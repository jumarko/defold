package com.dynamo.cr.ddfeditor;

import java.io.ByteArrayInputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.lang.reflect.Method;

import org.eclipse.core.commands.ExecutionException;
import org.eclipse.core.commands.operations.IOperationApprover;
import org.eclipse.core.commands.operations.IOperationHistory;
import org.eclipse.core.commands.operations.IOperationHistoryListener;
import org.eclipse.core.commands.operations.IUndoableOperation;
import org.eclipse.core.commands.operations.OperationHistoryEvent;
import org.eclipse.core.commands.operations.UndoContext;
import org.eclipse.core.resources.IContainer;
import org.eclipse.core.resources.IFile;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.dialogs.ErrorDialog;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.IFileEditorInput;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.forms.widgets.Form;
import org.eclipse.ui.forms.widgets.FormToolkit;
import org.eclipse.ui.operations.LinearUndoViolationUserApprover;
import org.eclipse.ui.operations.RedoActionHandler;
import org.eclipse.ui.operations.UndoActionHandler;
import org.eclipse.ui.part.EditorPart;

import com.dynamo.cr.editor.core.EditorCorePlugin;
import com.dynamo.cr.editor.core.EditorUtil;
import com.dynamo.cr.editor.core.IResourceType;
import com.dynamo.cr.editor.core.IResourceTypeRegistry;
import com.dynamo.cr.protobind.MessageNode;
import com.google.protobuf.GeneratedMessage;
import com.google.protobuf.GeneratedMessage.Builder;
import com.google.protobuf.TextFormat;

public abstract class DdfEditor extends EditorPart implements IOperationHistoryListener {

    private MessageNode message;
    private UndoContext undoContext;
    private UndoActionHandler undoAction;
    private RedoActionHandler redoAction;
    private int cleanUndoStackDepth = 0;
    private IResourceType resourceType;
    private ProtoTreeEditor protoTreeEditor;
    private IContainer contentRoot;
    private Form form;

    public DdfEditor(String extension) {
        IResourceTypeRegistry regist = EditorCorePlugin.getDefault().getResourceTypeRegistry();
        this.resourceType = regist.getResourceTypeFromExtension(extension);
        if (this.resourceType == null)
            throw new RuntimeException("Missing resource type for: " + extension);
    }

    public void executeOperation(IUndoableOperation operation) {
        IOperationHistory history = PlatformUI.getWorkbench().getOperationSupport().getOperationHistory();
        operation.addContext(undoContext);
        try
        {
            history.execute(operation, null, null);
        } catch (ExecutionException e)
        {
            e.printStackTrace();
        }
    }

    @Override
    public void doSave(IProgressMonitor monitor) {
        IFileEditorInput input = (IFileEditorInput) getEditorInput();

        String messageString = TextFormat.printToString(message.build());
        ByteArrayInputStream stream = new ByteArrayInputStream(messageString.getBytes());

        try {
            input.getFile().setContents(stream, false, true, monitor);
            IOperationHistory history = PlatformUI.getWorkbench().getOperationSupport().getOperationHistory();
            cleanUndoStackDepth = history.getUndoHistory(undoContext).length;
            firePropertyChange(PROP_DIRTY);
        } catch (CoreException e) {
            e.printStackTrace();
            Status status = new Status(IStatus.ERROR, "com.dynamo.cr.ddfeditor", 0, e.getMessage(), null);
            ErrorDialog.openError(Display.getCurrent().getActiveShell(), "Unable to save file", "Unable to save file", status);
        }
    }

    @Override
    public void doSaveAs() {
    }

    @Override
    public void init(IEditorSite site, IEditorInput input)
            throws PartInitException {

        setSite(site);
        setInput(input);
        setPartName(input.getName());

        IFileEditorInput i = (IFileEditorInput) input;
        IFile file = i.getFile();

        this.contentRoot = EditorUtil.findContentRoot(file);

        try {
            Reader reader = new InputStreamReader(file.getContents());

            try {
                Method m = this.resourceType.getMessageClass().getDeclaredMethod("newBuilder");
                @SuppressWarnings("rawtypes")
                GeneratedMessage.Builder builder = (Builder) m.invoke(null);

                try {
                    TextFormat.merge(reader, builder);

                    MessageNode message = new MessageNode(builder.build());
                    this.message = message;
                } finally {
                    reader.close();
                }
            } catch (Throwable e) {
                throw new PartInitException(e.getMessage(), e);
            }

        } catch (CoreException e) {
            throw new PartInitException(e.getMessage(), e);
        }

        this.undoContext = new UndoContext();
        IOperationHistory history = PlatformUI.getWorkbench().getOperationSupport().getOperationHistory();
        history.setLimit(this.undoContext, 100);
        history.addOperationHistoryListener(this);

        @SuppressWarnings("unused")
        IOperationApprover approver = new LinearUndoViolationUserApprover(this.undoContext, this);

        this.undoAction = new UndoActionHandler(this.getEditorSite(), this.undoContext);
        this.redoAction = new RedoActionHandler(this.getEditorSite(), this.undoContext);
    }

    @Override
    public boolean isDirty() {
        IOperationHistory history = PlatformUI.getWorkbench().getOperationSupport().getOperationHistory();
        return history.getUndoHistory(undoContext).length != cleanUndoStackDepth;
    }

    @Override
    public boolean isSaveAsAllowed() {
        return false;
    }

    @Override
    public void createPartControl(Composite parent) {
        FormToolkit toolkit = new FormToolkit(parent.getDisplay());
        this.form = toolkit.createForm(parent);

        IFileEditorInput input = (IFileEditorInput) getEditorInput();
        Image image = PlatformUI.getWorkbench().getEditorRegistry().getImageDescriptor(input.getName()).createImage();
        form.setImage(image);
        form.setText(this.resourceType.getName());
        toolkit.decorateFormHeading(form);
        form.getBody().setLayout(new FillLayout());

        protoTreeEditor = new ProtoTreeEditor(form.getBody(), toolkit, this.contentRoot, this.undoContext);

        protoTreeEditor.setInput(message, resourceType);
    }

    @Override
    public void setFocus() {
        this.form.getBody().setFocus();

        IActionBars action_bars = getEditorSite().getActionBars();
        action_bars.updateActionBars();

        action_bars.setGlobalActionHandler(ActionFactory.UNDO.getId(), undoAction);
        action_bars.setGlobalActionHandler(ActionFactory.REDO.getId(), redoAction);
    }

    @Override
    public void historyNotification(OperationHistoryEvent event) {
        firePropertyChange(PROP_DIRTY);
    }

}
