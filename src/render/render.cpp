#include <assert.h>
#include <dlib/hash.h>
#include <dlib/profile.h>
#include <graphics/graphics_device.h>
#include "renderinternal.h"
#include "model/model.h"


#include "rendertypes/rendertypemodel.h"
#include "rendertypes/rendertypetext.h"
#include "rendertypes/rendertypeparticle.h"

namespace dmRender
{
    struct RenderPass
    {
        RenderPassDesc          m_Desc;
        RenderContext           m_RenderContext;
        dmArray<RenderObject*>  m_InstanceList[2];
        uint16_t                m_RenderBuffer;
        uint16_t                m_InUse:1;
    };

    struct RenderWorld
    {
        dmArray<RenderObject*>  m_RenderObjectInstanceList;
        dmArray<RenderPass*>  	m_RenderPasses;
        RenderContext           m_RenderContext;

        SetObjectModel			m_SetObjectModel;
    };



    void FrameBeginDraw(RenderContext* rendercontext)
    {
    }
    void FrameEndDraw(RenderContext* rendercontext)
    {
    }

    void RenderPassBegin(RenderPass* rp)
    {
        rp->m_RenderBuffer = 1 - rp->m_RenderBuffer;
    }

    void RenderPassEnd(RenderPass* rp)
    {
        rp->m_InstanceList[rp->m_RenderBuffer].SetSize(0);
    }


    void RenderTypeBegin(RenderContext* rendercontext, RenderObjectType type)
    {
    	switch (type)
    	{
    		case RENDEROBJECT_TYPE_MODEL:
    		{
    			RenderTypeModelSetup(rendercontext);
    			break;
    		}
    		case RENDEROBJECT_TYPE_TEXT:
    		{
    		    RenderTypeTextSetup(rendercontext);
    		}
            case RENDEROBJECT_TYPE_PARTICLE:
            {
                RenderTypeParticleSetup(rendercontext);
            }
    		default:
    		{
    			break;
    		}
    	}
    }

    void RenderTypeEnd(RenderContext* rendercontext, RenderObjectType type)
    {

    }

    void rptestBegin(const RenderContext* rendercontext, const void* userdata)
    {
    }

    void rptestEnd(const RenderContext* rendercontext, const void* userdata)
    {
    }


    HRenderWorld NewRenderWorld(uint32_t max_instances, uint32_t max_renderpasses, SetObjectModel set_object_model)
    {
        RenderWorld* world = new RenderWorld;

        world->m_RenderObjectInstanceList.SetCapacity(max_instances);
        world->m_RenderObjectInstanceList.SetSize(0);
        world->m_RenderPasses.SetCapacity(max_renderpasses);
        world->m_RenderPasses.SetSize(0);
        world->m_SetObjectModel = set_object_model;
        world->m_RenderContext.m_GFXContext = dmGraphics::GetContext();

        return world;
    }

    void DeleteRenderWorld(HRenderWorld world)
    {
        delete world;
    }

    void AddRenderPass(HRenderWorld world, HRenderPass renderpass)
    {
        world->m_RenderPasses.Push(renderpass);
    }

    void UpdateContext(HRenderWorld world, RenderContext* rendercontext)
    {
        world->m_RenderContext = *rendercontext;
    }

    // this needs to go...
    void UpdateDeletedInstances(HRenderWorld world)
    {
        uint32_t size = world->m_RenderObjectInstanceList.Size();
        RenderObject** mem = new RenderObject*[size+1];

        dmArray<RenderObject*> temp_list(mem, 0, size+1);

        for (uint32_t i=0; i<size; i++)
        {
            RenderObject* ro = world->m_RenderObjectInstanceList[i];
            if (ro->m_MarkForDelete)
            {
                delete ro;
            }
            else
            {
                temp_list.Push(ro);
            }
        }

        size = temp_list.Size();
        world->m_RenderObjectInstanceList.SetSize(size);
        for (uint32_t i=0; i<size; i++)
        {
            world->m_RenderObjectInstanceList[i] = temp_list[i];
        }

        delete [] mem;
    }


    void AddToRender(HRenderWorld local_world, HRenderWorld world)
    {
        UpdateDeletedInstances(local_world);

        for (uint32_t i=0; i<local_world->m_RenderObjectInstanceList.Size(); i++)
        {
            RenderObject* ro = local_world->m_RenderObjectInstanceList[i];
            assert(ro->m_MarkForDelete == 0);

            for (uint32_t j=0; j<world->m_RenderPasses.Size(); j++)
            {
                RenderPass* rp = world->m_RenderPasses[j];
                if (rp->m_Desc.m_Predicate & ro->m_Mask)
                {
                    if (local_world->m_SetObjectModel)
                        local_world->m_SetObjectModel(0x0, ro->m_Go, &ro->m_Rot, &ro->m_Pos);
                    AddRenderObject(rp, ro);
                }
            }
        }
    }



    void Update(HRenderWorld world, float dt)
    {
        DM_PROFILE(Render, "Update");

    	FrameBeginDraw(&world->m_RenderContext);

    	for (uint32_t pass=0; pass<world->m_RenderPasses.Size(); pass++)
    	{
    		RenderPass* rp = world->m_RenderPasses[pass];
    		RenderPassBegin(rp);

    		if (rp->m_Desc.m_BeginFunc)
    			rp->m_Desc.m_BeginFunc(&world->m_RenderContext, 0x0);


    		// array is now a list of instances for the active render pass
    		dmArray<RenderObject*> *array = &rp->m_InstanceList[rp->m_RenderBuffer];
    		if (array->Size())
    		{
    			int old_type = -1;
				RenderObject** rolist = &array->Front();
				for (uint32_t e=0; e < array->Size(); e++, rolist++)
				{
					RenderObject* ro = *rolist;
					if (!ro->m_Enabled)
					    continue;

					// ro's can be marked for delete between this code and being added for rendering
					if (ro->m_MarkForDelete)
					    continue;

					// check if we need to change render type and run its setup func
					if (old_type != (int)ro->m_Type)
					{
						// change type
						RenderTypeBegin(&rp->m_RenderContext, ro->m_Type);
					}
					switch (ro->m_Type)
					{
						case RENDEROBJECT_TYPE_MODEL:
						{
							RenderTypeModelDraw(&rp->m_RenderContext, ro);
							break;
						}
						case RENDEROBJECT_TYPE_TEXT:
						{
						    RenderTypeTextDraw(&rp->m_RenderContext, ro);
						}
                        case RENDEROBJECT_TYPE_PARTICLE:
                        {
                            RenderTypeParticleDraw(&rp->m_RenderContext, ro);
                        }
						default:
						{
							break;
						}
					}

					if (old_type != (int)ro->m_Type)
					{
						RenderTypeEnd(&rp->m_RenderContext, ro->m_Type);
						old_type = ro->m_Type;
					}

				}
    		}


    		if (rp->m_Desc.m_EndFunc)
    			rp->m_Desc.m_EndFunc(&world->m_RenderContext, 0x0);

    		RenderPassEnd(rp);
    	}

    	FrameEndDraw(&world->m_RenderContext);

    	world->m_RenderObjectInstanceList.SetSize(0);
    	return;

    }

    HRenderObject NewRenderObjectInstance(HRenderWorld world, void* resource, void* go, uint64_t mask, RenderObjectType type)
    {
    	RenderObject* ro = new RenderObject;
    	ro->m_Data = resource;
    	ro->m_Type = type;
    	ro->m_Go = go;
    	ro->m_MarkForDelete = 0;
    	ro->m_Enabled = 1;
    	ro->m_Mask = mask;

    	if (type == RENDEROBJECT_TYPE_MODEL)
    	{
    		dmModel::Model* model = (dmModel::Model*)resource;
    		uint32_t reg;

            reg = DIFFUSE_COLOR;
            ro->m_Colour[reg] = dmGraphics::GetMaterialFragmentProgramConstant(dmModel::GetMaterial(model), reg);
            reg = EMISSIVE_COLOR;
            ro->m_Colour[reg] = dmGraphics::GetMaterialFragmentProgramConstant(dmModel::GetMaterial(model), reg);
            reg = SPECULAR_COLOR;
            ro->m_Colour[reg] = dmGraphics::GetMaterialFragmentProgramConstant(dmModel::GetMaterial(model), reg);
    	}


    	world->m_RenderObjectInstanceList.Push(ro);
    	return ro;
    }

    void SetData(HRenderObject ro, void* data)
    {
        ro->m_Data = data;
    }

    void SetGameObject(HRenderObject ro, void* go)
    {
        ro->m_Go = go;
    }

    void Disable(HRenderObject ro)
    {
        ro->m_Enabled = 0;
    }
    void Enable(HRenderObject ro)
    {
        ro->m_Enabled = 1;

    }
    bool IsEnabled(HRenderObject ro)
    {
        return ro->m_Enabled == true;
    }

    void DeleteRenderObject(HRenderWorld world, HRenderObject ro)
    {
    	// mark for delete
    	ro->m_MarkForDelete = 1;
    }

    void SetPosition(HRenderObject ro, Vector4 pos)
    {
    	// double buffering
    }
    void SetRotation(HRenderObject ro, Quat rot)
    {
    	// double buffering
    }

    void SetColor(HRenderObject ro, Vector4 color, ColorType color_type)
    {
        ro->m_Colour[color_type] = color;
    }


    void SetViewMatrix(HRenderPass renderpass, Matrix4* viewmatrix)
    {
        renderpass->m_RenderContext.m_View = *viewmatrix;
    }

    void SetViewProjectionMatrix(HRenderPass renderpass, Matrix4* viewprojectionmatrix)
    {
        renderpass->m_RenderContext.m_ViewProj = *viewprojectionmatrix;
    }


    HRenderPass NewRenderPass(RenderPassDesc* desc)
    {
        RenderPass* rp = new RenderPass;

        memcpy((void*)&rp->m_Desc, (void*)desc, sizeof(RenderPassDesc) );
        rp->m_RenderBuffer = 1;
        rp->m_InUse = 1;

        rp->m_InstanceList[0].SetCapacity(desc->m_Capacity);
        rp->m_InstanceList[1].SetCapacity(desc->m_Capacity);

        // separate context per renderpass possibly?
        rp->m_RenderContext.m_GFXContext = dmGraphics::GetContext();
        return rp;
    }

    void DeleteRenderPass(HRenderPass renderpass)
    {
        delete renderpass;
    }

    void DisableRenderPass(HRenderPass renderpass)
    {

    }

    void EnableRenderPass(HRenderPass renderpass)
    {

    }

    void AddRenderObject(HRenderPass renderpass, HRenderObject renderobject)
    {
        renderpass->m_InstanceList[!renderpass->m_RenderBuffer].Push(renderobject);
    }

}
