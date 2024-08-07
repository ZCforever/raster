

#include "layer2d.h"

#include "../../../ImGui/imgui.h"

#define UNIFORM_CLAUSE(t_uniform, t_type) \
    if (t_uniform.value.type() == typeid(t_type)) GPU::SetShaderUniform(pipeline.fragment, t_uniform.name, std::any_cast<t_type>(t_uniform.value))

namespace Raster {


    std::optional<Pipeline> Layer2D::s_nullShapePipeline;

    Layer2D::Layer2D() {
        NodeBase::Initialize();

        AddInputPin("Base");
        AddOutputPin("Framebuffer");

        SetupAttribute("Base", Framebuffer());
        SetupAttribute("Color", glm::vec4(1));
        SetupAttribute("Transform", Transform2D());
        SetupAttribute("UVTransform", Transform2D());
        SetupAttribute("Texture", Texture());
        SetupAttribute("Shape", SDFShape());
        SetupAttribute("SamplerSettings", SamplerSettings());
        SetupAttribute("MaintainUVRange", true);

        if (!s_nullShapePipeline.has_value()) {
            s_nullShapePipeline = GeneratePipelineFromShape(SDFShape()).pipeline;
        }

        this->m_sampler = GPU::GenerateSampler();
    }

    Layer2D::~Layer2D() {
        GPU::DestroySampler(m_sampler);
    }

    AbstractPinMap Layer2D::AbstractExecute(AbstractPinMap t_accumulator) {
        AbstractPinMap result = {};

        auto& project = Workspace::s_project.value();

        auto& framebuffer = m_managedFramebuffer.Get(GetAttribute<Framebuffer>("Base"));
        auto transformCandidate = GetAttribute<Transform2D>("Transform");
        auto colorCandidate = GetAttribute<glm::vec4>("Color");
        auto textureCandidate = GetAttribute<Texture>("Texture");
        auto samplerSettingsCandidate = GetAttribute<SamplerSettings>("SamplerSettings");
        auto uvTransformCandidate = GetAttribute<Transform2D>("UVTransform");
        auto maintainUVRangeCandidate = GetAttribute<bool>("MaintainUVRange");
        auto shapeCandidate = GetShape();
        auto pipelineCandidate = GetPipeline();

        if (pipelineCandidate && transformCandidate.has_value() && colorCandidate.has_value() && textureCandidate.has_value() && samplerSettingsCandidate.has_value() && uvTransformCandidate.has_value() && maintainUVRangeCandidate.has_value() && shapeCandidate.has_value()) {
            auto& pipeline = pipelineCandidate.value();
            auto& transform = transformCandidate.value();
            auto& color = colorCandidate.value();
            auto& texture = textureCandidate.value();
            auto& samplerSettings = samplerSettingsCandidate.value();
            auto uvTransform = uvTransformCandidate.value();
            auto& maintainUVRange = maintainUVRangeCandidate.value();
            auto& shape = shapeCandidate.value();
            uvTransform.size = 1.0f / uvTransform.size;

            uvTransform.anchor.x *= -1;
            uvTransform.anchor *= 0.5f;

            uvTransform.position.x *= -1;
            uvTransform.position *= 0.5f;

            GPU::BindFramebuffer(framebuffer);
            GPU::BindPipeline(pipeline);


            GPU::SetShaderUniform(pipeline.vertex, "uMatrix", project.GetProjectionMatrix() * transform.GetTransformationMatrix());

            GPU::SetShaderUniform(pipeline.fragment, "uMaintainUVRange", maintainUVRange);
            GPU::SetShaderUniform(pipeline.fragment, "uUVPosition", uvTransform.position);
            GPU::SetShaderUniform(pipeline.fragment, "uUVSize", uvTransform.size);
            GPU::SetShaderUniform(pipeline.fragment, "uUVAngle", glm::radians(uvTransform.angle));
            GPU::SetShaderUniform(pipeline.fragment, "uUVAnchor", uvTransform.anchor);
            GPU::SetShaderUniform(pipeline.fragment, "uColor", color);
            GPU::SetShaderUniform(pipeline.fragment, "uTextureAvailable", texture.handle ? 1 : 0);

            SetShapeUniforms(shape, pipeline);

            if (texture.handle) {
                GPU::BindTextureToShader(pipeline.fragment, "uTexture", texture, 0);
                GPU::BindSampler(m_sampler, 0);
                bool filteringModeMatches = m_sampler.magnifyMode == samplerSettings.filteringMode && m_sampler.minifyMode == samplerSettings.filteringMode;
                bool wrappingModeMatches = m_sampler.sMode == samplerSettings.wrappingMode && m_sampler.tMode == samplerSettings.wrappingMode;
                if (!filteringModeMatches || !wrappingModeMatches) {
                    GPU::SetSamplerTextureFilteringMode(m_sampler, TextureFilteringOperation::Magnify, samplerSettings.filteringMode);
                    GPU::SetSamplerTextureFilteringMode(m_sampler, TextureFilteringOperation::Minify, samplerSettings.filteringMode);

                    GPU::SetSamplerTextureWrappingMode(m_sampler, TextureWrappingAxis::S, samplerSettings.wrappingMode);
                    GPU::SetSamplerTextureWrappingMode(m_sampler, TextureWrappingAxis::T, samplerSettings.wrappingMode);
                }
            }
            GPU::DrawArrays(6);

            GPU::BindSampler(std::nullopt);

            TryAppendAbstractPinMap(result, "Framebuffer", framebuffer);
        }

        return result;
    }

    void Layer2D::SetShapeUniforms(SDFShape t_shape, Pipeline pipeline) {
        print("== BEGIN UNIFORMS ==");
        for (auto& uniform : t_shape.uniforms) {
            DUMP_VAR(uniform.name);
            if (uniform.value.type() == typeid(float)) {
                DUMP_VAR(std::any_cast<float>(uniform.value));
            }
            UNIFORM_CLAUSE(uniform, float);
            UNIFORM_CLAUSE(uniform, int);
            UNIFORM_CLAUSE(uniform, glm::vec2);
            UNIFORM_CLAUSE(uniform, glm::vec3);
            UNIFORM_CLAUSE(uniform, glm::vec4);
            UNIFORM_CLAUSE(uniform, glm::mat4);
        }
        print("== END UNIFORMS ==");
    }

    void Layer2D::AbstractRenderProperties() {
        RenderAttributeProperty("Transform");
        RenderAttributeProperty("UVTransform");
        RenderAttributeProperty("Color");
        RenderAttributeProperty("MaintainUVRange");
        RenderAttributeProperty("SamplerSettings");
    }

    std::optional<Pipeline> Layer2D::GetPipeline() {
        auto shapeCandidate = GetShape();
        if (!shapeCandidate.has_value()) return std::nullopt;
        auto& shape = shapeCandidate.value();
        if (shape.uniforms.empty()) {
            if (m_pipeline.has_value()) {
                GPU::DestroyPipeline(m_pipeline.value().pipeline);
                m_pipeline = std::nullopt;
            }
            return s_nullShapePipeline;
        }
        if (!m_pipeline.has_value()) {
            m_pipeline = GeneratePipelineFromShape(shape);
        }

        auto& pipeline = m_pipeline.value();
        if (pipeline.shape.id != shape.id) {
            GPU::DestroyPipeline(pipeline.pipeline);
            m_pipeline = GeneratePipelineFromShape(shape);
        }
        return m_pipeline.value().pipeline;
    }

    SDFShapePipeline Layer2D::GeneratePipelineFromShape(SDFShape t_shape) {
        std::string uniformsResult = "";
        for (auto& uniform : t_shape.uniforms) {
            uniformsResult += "uniform " + uniform.type + " " + uniform.name + ";\n";
        }

        static std::optional<std::string> s_shaderBase;
        if (!s_shaderBase.has_value()) {
            s_shaderBase = ReadFile(GPU::GetShadersPath() + "layer2d/shader_base.frag");
        }
        std::string shaderBase = s_shaderBase.value_or("");
        shaderBase = ReplaceString(shaderBase, "SDF_UNIFORMS_PLACEHOLDER", uniformsResult);
        shaderBase = ReplaceString(shaderBase, "SDF_DISTANCE_FUNCTION_PLACEHOLDER", t_shape.distanceFunctionName);
        shaderBase = ReplaceString(shaderBase, "SDF_DISTANCE_FUNCTIONS_PLACEHOLDER", t_shape.distanceFunctionCode);

        std::string clearShaderFile = "layer2d/generated_shape_pipeline" + std::to_string(nodeID);
        std::string shaderFile = GPU::GetShadersPath() + clearShaderFile + ".frag";
        WriteFile(shaderFile, shaderBase);

        DUMP_VAR(t_shape.distanceFunctionCode);

        Pipeline generatedPipeline = GPU::GeneratePipeline(
            GPU::GenerateShader(ShaderType::Vertex, "layer2d/shader"),
            GPU::GenerateShader(ShaderType::Fragment, clearShaderFile)
        );

        std::filesystem::remove(shaderFile);

        return SDFShapePipeline{
            .shape = t_shape,
            .pipeline = generatedPipeline
        };
    }

    std::optional<SDFShape> Layer2D::GetShape() {
        auto shapeCandidate = GetDynamicAttribute("Shape");
        if (shapeCandidate.has_value() && shapeCandidate.value().type() == typeid(SDFShape)) {
            return std::any_cast<SDFShape>(shapeCandidate.value());
        }    
        return std::nullopt;
    }

    bool Layer2D::AbstractDetailsAvailable() {
        return false;
    }

    std::string Layer2D::AbstractHeader() {
        return "Layer2D";
    }

    std::string Layer2D::Icon() {
        return ICON_FA_IMAGE " " ICON_FA_UP_DOWN_LEFT_RIGHT;
    }

    std::optional<std::string> Layer2D::Footer() {
        return std::nullopt;
    }
}

extern "C" {
    Raster::AbstractNode SpawnNode() {
        return (Raster::AbstractNode) std::make_shared<Raster::Layer2D>();
    }

    Raster::NodeDescription GetDescription() {
        return Raster::NodeDescription{
            .prettyName = "Layer2D",
            .packageName = RASTER_PACKAGED "layer2d",
            .category = Raster::DefaultNodeCategories::s_rendering
        };
    }
}