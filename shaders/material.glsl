struct Material {
    vec4 base_color_factor;
    vec4 emissive_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_scale;
    int albedo_image;
    int albedo_sampler;
    int metallic_roughness_image;
    int metallic_roughness_sampler;
    int normal_image;
    int normal_sampler;
    int emissive_image;
    int emissive_sampler;
    int occlusion_image;
    int occlusion_sampler;
};

layout(buffer_reference, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
