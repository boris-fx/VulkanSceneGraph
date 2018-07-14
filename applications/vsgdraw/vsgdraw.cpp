#include <vsg/core/ref_ptr.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/core/Result.h>

#include <vsg/utils/CommandLine.h>

#include <vsg/vk/Instance.h>
#include <vsg/vk/Surface.h>
#include <vsg/vk/Swapchain.h>
#include <vsg/vk/CmdDraw.h>
#include <vsg/vk/ShaderModule.h>
#include <vsg/vk/RenderPass.h>
#include <vsg/vk/Pipeline.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/CommandPool.h>
#include <vsg/vk/Semaphore.h>
#include <vsg/vk/Buffer.h>
#include <vsg/vk/DeviceMemory.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <set>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace glfw
{

vsg::Names getInstanceExtensions()
{
    uint32_t glfw_count;
    const char** glfw_extensons = glfwGetRequiredInstanceExtensions(&glfw_count);
    return vsg::Names(glfw_extensons, glfw_extensons+glfw_count);
}

// forward declare
class GLFW_Instance;
static vsg::ref_ptr<glfw::GLFW_Instance> getGLFW_Instance();

class GLFW_Instance : public vsg::Object
{
public:
protected:
    GLFW_Instance()
    {
        std::cout<<"Calling glfwInit"<<std::endl;
        glfwInit();
    }

    virtual ~GLFW_Instance()
    {
        std::cout<<"Calling glfwTerminate()"<<std::endl;
        glfwTerminate();
    }

    friend vsg::ref_ptr<glfw::GLFW_Instance> getGLFW_Instance();
};

static vsg::ref_ptr<glfw::GLFW_Instance> getGLFW_Instance()
{
    static vsg::observer_ptr<glfw::GLFW_Instance> s_glfw_Instance = new glfw::GLFW_Instance;
    return s_glfw_Instance;
}

class Window : public vsg::Object
{
public:
    Window(uint32_t width, uint32_t height) : _instance(glfw::getGLFW_Instance())
    {
        std::cout<<"Calling glfwCreateWindow(..)"<<std::endl;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        _window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
    }

    operator GLFWwindow* () { return _window; }
    operator const GLFWwindow* () const { return _window; }

protected:
    virtual ~Window()
    {
        if (_window)
        {
            std::cout<<"Calling glfwDestroyWindow(_window);"<<std::endl;
            glfwDestroyWindow(_window);
        }
    }

    vsg::ref_ptr<glfw::GLFW_Instance>   _instance;
    GLFWwindow*                         _window;
};


class GLFWSurface : public vsg::Surface
{
public:

    GLFWSurface(vsg::Instance* instance, GLFWwindow* window, vsg::AllocationCallbacks* allocator=nullptr) :
        vsg::Surface(instance, VK_NULL_HANDLE, allocator)
    {
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != VK_SUCCESS)
        {
            std::cout<<"Failed to create window surface"<<std::endl;
        }
        else
        {
            std::cout<<"Created window surface"<<std::endl;
        }
    }
};

}

template<typename T>
void print(std::ostream& out, const std::string& description, const T& names)
{
    out<<description<<".size()= "<<names.size()<<std::endl;
    for (const auto& name : names)
    {
        out<<"    "<<name<<std::endl;
    }
}

namespace vsg
{

    template< typename ... Args >
    std::string make_string(Args const& ... args )
    {
        std::ostringstream stream;
        using List= int[];
        (void) List {0, ( (void)(stream << args), 0 ) ... };

        return stream.str();
    }

    // should this be called GraphicsPipelineState?
    class PipelineState : public Object
    {
    public:
        PipelineState() {}

        enum Type
        {
            SHADER_STAGE,
            VERTEX_INPUT_STATE,
            INPUT_ASSEMBLY_STATE,
            VIEWPORT_STATE,
            RASTERIZATION_STATE,
            MULTISAMPLE_STATE,
            COLOR_BLEND_STATE
        };

        virtual Type getType() const = 0;

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const = 0;

    protected:
        virtual ~PipelineState() {}
    };

    class ShaderStages : public PipelineState
    {
    public:
        ShaderStages(const ShaderModules& shaderModules)
        {
            setShaderModules(shaderModules);
        }

        virtual Type getType() const { return SHADER_STAGE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.stageCount = size();
            pipelineInfo.pStages = data();
        }

        void setShaderModules(const ShaderModules& shaderModules) { _shaderModules = shaderModules; update(); }
        const ShaderModules& getShaderModules() const { return _shaderModules; }

        void update()
        {
            _stages.resize(_shaderModules.size());
            for (size_t i=0; i<_shaderModules.size(); ++i)
            {
                VkPipelineShaderStageCreateInfo& stageInfo = (_stages)[i];
                ShaderModule* sm = _shaderModules[i];
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = sm->getStage();
                stageInfo.module = *sm;
                stageInfo.pName = sm->getEntryPointName().c_str();
            }
        }

        std::size_t size() const { return _stages.size(); }

        VkPipelineShaderStageCreateInfo* data() { return _stages.data(); }
        const VkPipelineShaderStageCreateInfo* data() const { return _stages.data(); }

    protected:
        virtual ~ShaderStages()
        {
            std::cout<<"~ShaderStages()"<<std::endl;
        }

        using Stages = std::vector<VkPipelineShaderStageCreateInfo>;
        Stages          _stages;
        ShaderModules   _shaderModules;
    };

    class VertexInputState : public PipelineState
    {
    public:
        using Bindings = std::vector<VkVertexInputBindingDescription>;
        using Attributes = std::vector<VkVertexInputAttributeDescription>;

        VertexInputState() :
            _info {}
        {
            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            _info.vertexBindingDescriptionCount = 0;
            _info.vertexAttributeDescriptionCount = 0;
        }

        VertexInputState(const Bindings& bindings, const Attributes& attributes) :
            _bindings(bindings),
            _attributes(attributes),
            _info {}
        {
            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            _info.vertexBindingDescriptionCount = _bindings.size();
            _info.pVertexBindingDescriptions = _bindings.data();
            _info.vertexAttributeDescriptionCount = _attributes.size();
            _info.pVertexAttributeDescriptions = _attributes.data();
        }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pVertexInputState = info();
        }

        virtual Type getType() const { return VERTEX_INPUT_STATE; }

        const Bindings& geBindings() { return _bindings; }

        const Attributes& getAttributes() const { return _attributes; }

        VkPipelineVertexInputStateCreateInfo*  info() { return &_info; }
        const VkPipelineVertexInputStateCreateInfo*  info() const { return &_info; }

    protected:
        virtual ~VertexInputState()
        {
            std::cout<<"~VertexInputState()"<<std::endl;
        }

        Bindings                                _bindings;
        Attributes                              _attributes;
        VkPipelineVertexInputStateCreateInfo    _info;
    };

    class InputAssemblyState : public PipelineState
    {
    public:
        InputAssemblyState() :
            _info {}
        {
            // primitive input
            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            _info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            _info.primitiveRestartEnable = VK_FALSE;
        }

        virtual Type getType() const { return INPUT_ASSEMBLY_STATE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pInputAssemblyState = info();
        }

        const VkPipelineInputAssemblyStateCreateInfo* info() const { return &_info; }

    protected:
        virtual ~InputAssemblyState()
        {
            std::cout<<"~InputAssemblyState()"<<std::endl;
        }

        VkPipelineInputAssemblyStateCreateInfo _info;
    };


    class ViewportState : public PipelineState
    {
    public:
        ViewportState(const VkExtent2D& extent) :
            _viewport{},
            _scissor{},
            _info {}
        {
            _viewport.x = 0.0f;
            _viewport.y = 0.0f;
            _viewport.width = static_cast<float>(extent.width);
            _viewport.height = static_cast<float>(extent.height);

            _scissor.offset = {0, 0};
            _scissor.extent = extent;

            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            _info.viewportCount = 1;
            _info.pViewports = &_viewport;
            _info.scissorCount = 1;
            _info.pScissors = &_scissor;
        }

        virtual Type getType() const { return VIEWPORT_STATE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pViewportState = info();
        }

        VkViewport& getViewport() { return _viewport; }
        VkRect2D& getScissor() { return _scissor; }

        VkPipelineViewportStateCreateInfo* info() { return &_info; }
        const VkPipelineViewportStateCreateInfo* info() const { return &_info; }

    protected:
        virtual ~ViewportState()
        {
            std::cout<<"~ViewportState()"<<std::endl;
        }

        VkViewport                          _viewport;
        VkRect2D                            _scissor;
        VkPipelineViewportStateCreateInfo   _info;
    };

    class RasterizationState : public PipelineState
    {
    public:
        RasterizationState() :
            _info {}
        {
            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            _info.depthClampEnable = VK_FALSE;
            _info.polygonMode = VK_POLYGON_MODE_FILL;
            _info.lineWidth = 1.0f;
            _info.cullMode = VK_CULL_MODE_BACK_BIT;
            _info.frontFace = VK_FRONT_FACE_CLOCKWISE;
            _info.depthBiasEnable = VK_FALSE;
        }

        virtual Type getType() const { return RASTERIZATION_STATE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pRasterizationState = info();
        }

        VkPipelineRasterizationStateCreateInfo* info() { return &_info; }
        const VkPipelineRasterizationStateCreateInfo* info() const { return &_info; }

    protected:
        virtual ~RasterizationState()
        {
            std::cout<<"~RasterizationState()"<<std::endl;
        }

        VkPipelineRasterizationStateCreateInfo _info;
    };

    class MultisampleState : public PipelineState
    {
    public:
        MultisampleState() :
            _info {}
        {
            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            _info.sampleShadingEnable =VK_FALSE;
            _info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        }

        virtual Type getType() const { return MULTISAMPLE_STATE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pMultisampleState = info();
        }

        VkPipelineMultisampleStateCreateInfo* info() { return &_info; }
        const VkPipelineMultisampleStateCreateInfo* info() const { return &_info; }

    protected:
        virtual ~MultisampleState()
        {
            std::cout<<"~MultisampleState()"<<std::endl;
        }

        VkPipelineMultisampleStateCreateInfo _info;
    };


    class ColorBlendState : public PipelineState
    {
    public:
        ColorBlendState() :
            _info {}
        {
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT |
                                                VK_COLOR_COMPONENT_A_BIT;

            _colorBlendAttachments.push_back(colorBlendAttachment);

            _info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            _info.logicOpEnable = VK_FALSE;
            _info.logicOp = VK_LOGIC_OP_COPY;
            _info.attachmentCount = _colorBlendAttachments.size();
            _info.pAttachments = _colorBlendAttachments.data();
            _info.blendConstants[0] = 0.0f;
            _info.blendConstants[1] = 0.0f;
            _info.blendConstants[2] = 0.0f;
            _info.blendConstants[3] = 0.0f;
        }

        virtual Type getType() const { return COLOR_BLEND_STATE; }

        virtual void apply(VkGraphicsPipelineCreateInfo& pipelineInfo) const
        {
            pipelineInfo.pColorBlendState = info();
        }

        using ColorBlendAttachments = std::vector<VkPipelineColorBlendAttachmentState>;

        const ColorBlendAttachments& getColorBlendAttachments() const { return _colorBlendAttachments; }

        VkPipelineColorBlendStateCreateInfo* info() { return &_info; }
        const VkPipelineColorBlendStateCreateInfo* info() const { return &_info; }

    protected:
        virtual ~ColorBlendState()
        {
            std::cout<<"~ColorBlendState()"<<std::endl;
        }

        ColorBlendAttachments _colorBlendAttachments;
        VkPipelineColorBlendStateCreateInfo _info;
    };

    using PipelineStates = std::vector<ref_ptr<PipelineState>>;
    using PipelineResult = vsg::Result<Pipeline, VkResult, VK_SUCCESS>;

    PipelineResult createGraphics(Device* device, RenderPass* renderPass, PipelineLayout* pipelineLayout, const PipelineStates& pipelineStates, AllocationCallbacks* allocator=nullptr)
    {
        if (!device || !renderPass || !pipelineLayout)
        {
            return PipelineResult("Error: vsg::createGraphics(...) failed to create graphics pipeline, undefined inputs.", VK_ERROR_INVALID_EXTERNAL_HANDLE);
        }

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = *pipelineLayout;
        pipelineInfo.renderPass = *renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        for (auto pipelineState : pipelineStates)
        {
            pipelineState->apply(pipelineInfo);
        }

        VkPipeline pipeline;
        VkResult result = vkCreateGraphicsPipelines(*device, VK_NULL_HANDLE, 1, &pipelineInfo, *allocator, &pipeline );
        if (result == VK_SUCCESS)
        {
            return new Pipeline(device, pipeline, allocator);
        }
        else
        {
            return PipelineResult("Error: vsg::createGraphics(...) failed to create VkPipeline.", result);
        }
    }

    using DispatchList = std::vector<ref_ptr<Dispatch>>;

    VkResult populateCommandBuffer(VkCommandBuffer commandBuffer, RenderPass* renderPass, Framebuffer* framebuffer, Swapchain* swapchain, const DispatchList& dispatchList)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        VkResult result;;
        if ((result = vkBeginCommandBuffer(commandBuffer, &beginInfo)) != VK_SUCCESS)
        {
            std::cout<<"Error: could not begin command buffer."<<std::endl;
            return result;
        }

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *renderPass;
        renderPassInfo.framebuffer = *framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain->getExtent();

        VkClearValue clearColor = {0.2f, 0.2f, 0.4f, 1.0f};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        for(auto dispatch : dispatchList)
        {
            dispatch->dispatch(commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);

        if ((result = vkEndCommandBuffer(commandBuffer)) != VK_SUCCESS)
        {
            std::cout<<"Error: could not end command buffer."<<std::endl;
            return result;
        }
    }

    using CommandBuffers = std::vector<VkCommandBuffer>;

    class VulkanWindowObjects : public Object
    {
    public:

        VulkanWindowObjects(PhysicalDevice* physicalDevice, Device* device, Surface* surface, CommandPool* commandPool, RenderPass* renderPass, const DispatchList& dispatchList, uint32_t width, uint32_t height)
        {
            // keep device and commandPool around to enable vkFreeCommandBuffers call in destructor
            _device = device;
            _commandPool = commandPool;
            _renderPass = renderPass;

            // create all the window related Vulkan objects
            swapchain = Swapchain::create(physicalDevice, device, surface, width, height);
            framebuffers = createFrameBuffers(device, swapchain, renderPass);

            std::cout<<"swapchain->getImageFormat()="<<swapchain->getImageFormat()<<std::endl;

            // setup command buffers
            {
                commandBuffers.resize(framebuffers.size());

                VkCommandBufferAllocateInfo allocateInfo = {};
                allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.commandPool = *commandPool;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = (uint32_t) commandBuffers.size();

                if (vkAllocateCommandBuffers(*device, &allocateInfo, commandBuffers.data()) == VK_SUCCESS)
                {
                    for(size_t i=0; i<commandBuffers.size(); ++i)
                    {
                        populateCommandBuffer(commandBuffers[i], renderPass, framebuffers[i], swapchain, dispatchList);
                    }
                }
                else
                {
                    std::cout<<"Error: could not allocate command buffers."<<std::endl;
                }
            }
        }

        ~VulkanWindowObjects()
        {
            vkFreeCommandBuffers(*_device, *_commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        }

        ref_ptr<Device>             _device;
        ref_ptr<CommandPool>        _commandPool;
        ref_ptr<RenderPass>         _renderPass;

        ref_ptr<Swapchain>          swapchain;
        Framebuffers                framebuffers;
        CommandBuffers              commandBuffers;
    };
}



int main(int argc, char** argv)
{
    bool debugLayer = false;
    uint32_t width = 800;
    uint32_t height = 600;
    int numFrames=-1;

    try
    {
        if (vsg::CommandLine::read(argc, argv, vsg::CommandLine::Match("--debug","-d"))) debugLayer = true;
        if (vsg::CommandLine::read(argc, argv, vsg::CommandLine::Match("--window","-w"), width, height)) {}
        if (vsg::CommandLine::read(argc, argv, "-f", numFrames)) {}
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    vsg::ref_ptr<glfw::Window> window = new glfw::Window(width, height);


    /////////////////////////////////////////////////////////////////////
    //
    // start of initialize vulkan
    //

    vsg::Names instanceExtensions = glfw::getInstanceExtensions();

    vsg::Names requestedLayers;
    if (debugLayer)
    {
        instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        requestedLayers.push_back("VK_LAYER_LUNARG_standard_validation");
    }

    vsg::Names validatedNames = vsg::validateInstancelayerNames(requestedLayers);

    vsg::Names deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    print(std::cout,"instanceExtensions",instanceExtensions);
    print(std::cout,"validatedNames",validatedNames);

    vsg::ref_ptr<vsg::Instance> instance = vsg::Instance::create(instanceExtensions, validatedNames);

    // use GLFW to create surface
    vsg::ref_ptr<glfw::GLFWSurface> surface = new glfw::GLFWSurface(instance, *window, nullptr);

    // set up device
    vsg::ref_ptr<vsg::PhysicalDevice> physicalDevice = vsg::PhysicalDevice::create(instance, surface);
    vsg::ref_ptr<vsg::Device> device = vsg::Device::create(instance, physicalDevice, validatedNames, deviceExtensions);


    vsg::ref_ptr<vsg::ShaderModule> vert = vsg::ShaderModule::read(device, VK_SHADER_STAGE_VERTEX_BIT, "main", "shaders/vert.spv");
    vsg::ref_ptr<vsg::ShaderModule> frag = vsg::ShaderModule::read(device, VK_SHADER_STAGE_FRAGMENT_BIT, "main", "shaders/frag.spv");
    if (!vert || !frag)
    {
        std::cout<<"Could not create shaders"<<std::endl;
        return 1;
    }
    vsg::ShaderModules shaderModules{vert, frag};
    vsg::ref_ptr<vsg::ShaderStages> shaderStages = new vsg::ShaderStages(shaderModules);

    VkQueue graphicsQueue = vsg::createDeviceQueue(*device, physicalDevice->getGraphicsFamily());
    VkQueue presentQueue = vsg::createDeviceQueue(*device, physicalDevice->getPresentFamily());
    if (!graphicsQueue || !presentQueue)
    {
        std::cout<<"Required graphics/present queue not available!"<<std::endl;
        return 1;
    }

    vsg::ref_ptr<vsg::CommandPool> commandPool = vsg::CommandPool::create(device, physicalDevice->getGraphicsFamily());
    vsg::ref_ptr<vsg::Semaphore> imageAvailableSemaphore = vsg::Semaphore::create(device);
    vsg::ref_ptr<vsg::Semaphore> renderFinishedSemaphore = vsg::Semaphore::create(device);


    // set up vertex arrays
    vsg::ref_ptr<vsg::vec2Array> vertices = new vsg::vec2Array(3);
    vsg::ref_ptr<vsg::vec3Array> colors = new vsg::vec3Array(3);

    vertices->set(0, {0.0f, -0.5f});
    vertices->set(1, {0.5f,  0.5f});
    vertices->set(2, {-0.5f, 0.5f});

    colors->set(0, {1.0f, 0.0f, 0.0f});
    colors->set(1, {0.0f, 1.0f, 0.0f});
    colors->set(2, {0.0f, 0.0f, 1.0f});

    vsg::ref_ptr<vsg::Buffer> vertexBuffer = vsg::Buffer::createVertexBuffer(device, vertices->dataSize()+colors->dataSize());
    vsg::ref_ptr<vsg::DeviceMemory> vertexBufferMemory =  vsg::DeviceMemory::create(physicalDevice, device, vertexBuffer);

    vkBindBufferMemory(*device, *vertexBuffer, *vertexBufferMemory, 0);
    vertexBufferMemory->copy(0, vertices->dataSize(), vertices->data());
    vertexBufferMemory->copy(vertices->dataSize(), colors->dataSize(), colors->data());

    vsg::ref_ptr<vsg::VertexBuffers> vertexBuffers = new vsg::VertexBuffers;
    vertexBuffers->add(vertexBuffer, 0);
    vertexBuffers->add(vertexBuffer, vertices->dataSize());

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{VkVertexInputBindingDescription{0, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}, VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
    vsg::VertexInputState::Attributes vertexAttrobiteDescriptions{VkVertexInputAttributeDescription{0, 0,VK_FORMAT_R32G32_SFLOAT, 0}, VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}};

    vsg::ref_ptr<vsg::CmdDraw> cmdDraw = new vsg::CmdDraw(3, 1, 0, 0);

    // setup pipeline
    vsg::PipelineStates pipelineStates;
    pipelineStates.push_back(shaderStages);
    pipelineStates.push_back(new vsg::VertexInputState(vertexBindingsDescriptions, vertexAttrobiteDescriptions));
    pipelineStates.push_back(new vsg::InputAssemblyState);
    pipelineStates.push_back(new vsg::ViewportState(VkExtent2D{width, height}));
    pipelineStates.push_back(new vsg::RasterizationState);
    pipelineStates.push_back(new vsg::MultisampleState);
    pipelineStates.push_back(new vsg::ColorBlendState);

    vsg::SwapChainSupportDetails supportDetails = vsg::querySwapChainSupport(*physicalDevice, *surface);
    VkSurfaceFormatKHR imageFormat = vsg::selectSwapSurfaceFormat(supportDetails);
    vsg::ref_ptr<vsg::RenderPass> renderPass = vsg::RenderPass::create(device, imageFormat.format);

    vsg::ref_ptr<vsg::PipelineLayout> pipelineLayout = new vsg::PipelineLayout(device);
    vsg::ref_ptr<vsg::Pipeline> pipeline = vsg::createGraphics(device, renderPass, pipelineLayout, pipelineStates);


    // set up what we want to render
    vsg::DispatchList dispatchList;
    dispatchList.push_back(pipeline);
    dispatchList.push_back(vertexBuffers);
    dispatchList.push_back(cmdDraw);

    vsg::ref_ptr<vsg::VulkanWindowObjects> vwo = new vsg::VulkanWindowObjects(physicalDevice, device, surface, commandPool, renderPass, dispatchList, width, height);

    //
    // end of initialize vulkan
    //
    /////////////////////////////////////////////////////////////////////


    // main loop
    while(!glfwWindowShouldClose(*window) && (numFrames<0 || (numFrames--)>0))
    {
        //std::cout<<"In main loop"<<std::endl;
        glfwPollEvents();

        bool needToRegerateVulkanWindowObjects = false;

        int new_width, new_height;
        glfwGetWindowSize(*window, &new_width, &new_height);
        if (new_width!=int(width) || new_height!=int(height))
        {
            std::cout<<"Warning: window resized to "<<new_width<<", "<<new_height<<std::endl;
            needToRegerateVulkanWindowObjects = true;
        }

        uint32_t imageIndex;
        if (!needToRegerateVulkanWindowObjects)
        {
            // drawFrame
            VkResult result = vkAcquireNextImageKHR(*device, *(vwo->swapchain), std::numeric_limits<uint64_t>::max(), *imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                needToRegerateVulkanWindowObjects = true;
                std::cout<<"Warning: Image out of data, need to recreate swap chain and assoicated dependencies."<<std::endl;
            }
            else if (result != VK_SUCCESS)
            {
                needToRegerateVulkanWindowObjects = true;
                std::cout<<"Warning: failed to aquire swap chain image."<<std::endl;
            }
        }

        if (needToRegerateVulkanWindowObjects)
        {
            vkDeviceWaitIdle(*device);

            // clean up previous VulkanWindowObjects
            vwo = nullptr;

            // create new VulkanWindowObjects
            width = new_width;
            height = new_height;
            vwo = new vsg::VulkanWindowObjects(physicalDevice, device, surface, commandPool, renderPass, dispatchList, width, height);

            VkResult result = vkAcquireNextImageKHR(*device, *(vwo->swapchain), std::numeric_limits<uint64_t>::max(), *imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
            if (result != VK_SUCCESS)
            {
                std::cout<<"Warning: could not recreate swap chain image."<<std::endl;
                return 1;
            }
        }

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {*imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(vwo->commandBuffers)[imageIndex];

        VkSemaphore signalSemaphores[] = {*renderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            std::cout<<"Error: failed to submit draw command buffer."<<std::endl;
            return 1;
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {*(vwo->swapchain)};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;

        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(presentQueue, &presentInfo);

        if (debugLayer)
        {
            vkQueueWaitIdle(presentQueue);
        }

    }

    vkDeviceWaitIdle(*device);

    // clean up done automatically thanks to ref_ptr<>
    return 0;
}