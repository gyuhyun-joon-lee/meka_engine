/*
 * Written by Gyuhyun Lee
 */
#include "hb_asset.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

internal void
flush_gpu_visible_buffer(GPUVisibleBuffer *buffer)
{
#if HB_WINDOWS || HB_LINUX
    // TODO(gh) This does nothing for now(unified memory), 
    // but should be implemented in other platforms
    assert(0);
#endif
}

internal TextureAsset2D
load_texture_asset(ThreadWorkQueue *gpu_work_queue, void *source, i32 width, i32 height, i32 bytes_per_pixel)
{
    TextureAsset2D result = {};

    assert(source);

    // Allocate the space in GPU and get the handle
    ThreadAllocateTexture2DData allocate_texture2D_data = {};
    allocate_texture2D_data.handle_to_populate = &result.handle;
    allocate_texture2D_data.width = width;
    allocate_texture2D_data.height = height;
    allocate_texture2D_data.bytes_per_pixel = bytes_per_pixel;

    // Load the file into texture
    gpu_work_queue->add_thread_work_queue_item(gpu_work_queue, 0, GPUWorkType_AllocateTexture2D, &allocate_texture2D_data);
    gpu_work_queue->complete_all_thread_work_queue_items(gpu_work_queue, false);

    ThreadWriteEntireTexture2D write_entire_texture2D_data = {};
    write_entire_texture2D_data.handle = result.handle;
    write_entire_texture2D_data.source = source;
    write_entire_texture2D_data.width = width;
    write_entire_texture2D_data.height = height;
    write_entire_texture2D_data.bytes_per_pixel = bytes_per_pixel;

    gpu_work_queue->add_thread_work_queue_item(gpu_work_queue, 0, GPUWorkType_WriteEntireTexture2D, &write_entire_texture2D_data);
    gpu_work_queue->complete_all_thread_work_queue_items(gpu_work_queue, false);

    assert(result.handle);

    return result;
}

internal GPUVisibleBuffer
get_gpu_visible_buffer(ThreadWorkQueue *gpu_work_queue, u64 size)
{
    GPUVisibleBuffer result = {};

    ThreadAllocateBufferData allocate_buffer_data = {};
    allocate_buffer_data.handle_to_populate = &result.handle;
    allocate_buffer_data.memory_to_populate = &result.memory;
    allocate_buffer_data.size_to_allocate = size;
    gpu_work_queue->add_thread_work_queue_item(gpu_work_queue, 0, GPUWorkType_AllocateBuffer, &allocate_buffer_data);
    gpu_work_queue->complete_all_thread_work_queue_items(gpu_work_queue, false);

    return result;
}


// TODO(gh) Only loads VertexPN vertices
internal void
load_mesh_asset(GameAssets *assets, ThreadWorkQueue *gpu_work_queue, 
                AssetTag tag,
                VertexPN *vertices, u32 vertex_count, 
                u32 *indices, u32 index_count)
{
    // NOTE(gh) Couldn't find the asset that was already loaded,
    // so need to load a new asset

    MeshAsset *asset = assets->mesh_assets + assets->populated_mesh_asset++;
    assert(assets->populated_mesh_asset <= array_count(assets->mesh_assets));

    // NOTE(gh) Load the vertex buffer
    {
        u64 vertex_buffer_size = sizeof(vertices[0])*vertex_count;
        asset->vertex_buffer = get_gpu_visible_buffer(gpu_work_queue, vertex_buffer_size);

        memcpy(asset->vertex_buffer.memory, vertices, vertex_buffer_size);
        flush_gpu_visible_buffer(&asset->vertex_buffer);

        asset->vertex_count = vertex_count;
    }
     
    // NOTE(gh) Load the index buffer
    {
        u64 index_buffer_size = sizeof(indices[0])*index_count;
        asset->index_buffer = get_gpu_visible_buffer(gpu_work_queue, index_buffer_size);

        memcpy(asset->index_buffer.memory, indices, index_buffer_size);
        flush_gpu_visible_buffer(&asset->index_buffer);

        asset->index_count = index_count;
    }

    asset->tag = tag;
}

internal MeshAsset *
get_mesh_asset(GameAssets *asset, u32 *mesh_assetID, AssetTag tag)
{
    MeshAsset *result = 0;

    if(mesh_assetID && *mesh_assetID != 0)
    {
        // There is already a populated assetID.
        // This is possible if the entity was a softbody which needs seperate
        // asset(i.e vertices) per entity, or was previously rendered
        result = asset->mesh_assets + *mesh_assetID;
    }
    else
    {
        for(u32 i = 0;
                i < asset->populated_mesh_asset;
                ++i)
        {
            MeshAsset *mesh_asset = asset->mesh_assets + i;
            if(mesh_asset->tag == tag)
            {
                result = mesh_asset;
                break;
            }
        }
    }

    assert(result);

    return result;
}

internal void
begin_load_font(LoadFontInfo *load_font_info, FontAsset *font_asset, const char *file_path, PlatformAPI *platform_api, u32 max_glyph_count, f32 desired_font_height_px)
{
    load_font_info->font_asset = font_asset;
    load_font_info->font_asset->max_glyph_count = max_glyph_count;

    PlatformReadFileResult font_data = platform_api->read_file(file_path);
    load_font_info->desired_font_height_px = desired_font_height_px;

    stbtt_InitFont(&load_font_info->font_info, font_data.memory, 0);
    load_font_info->font_scale = stbtt_ScaleForPixelHeight(&load_font_info->font_info, desired_font_height_px);

    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&load_font_info->font_info, &ascent, &descent, &line_gap);

    font_asset->ascent_from_baseline = load_font_info->font_scale * ascent;
    font_asset->descent_from_baseline = -1.0f*load_font_info->font_scale * descent; // stb library gives us negative value, but we want positive value for this
    font_asset->line_gap = load_font_info->font_scale*line_gap;

    // TODO(gh) We should get rid of these mallocs, for sure.
    u32 codepoint_to_glyphID_table_size = sizeof(u32) * MAX_UNICODE_CODEPOINT;
    font_asset->codepoint_to_glyphID_table = (u16 *)malloc(sizeof(u16) * MAX_UNICODE_CODEPOINT);
    zero_memory(font_asset->codepoint_to_glyphID_table, codepoint_to_glyphID_table_size);

    font_asset->glyph_assets = (GlyphAsset *)malloc(sizeof(GlyphAsset) * max_glyph_count);
    font_asset->kerning_advances = (f32 *)malloc(sizeof(f32) * max_glyph_count * max_glyph_count);
}

#if 1 
internal void
end_load_font(LoadFontInfo *load_font_info)
{
    FontAsset *font_asset = load_font_info->font_asset;
    for(u32 i = 0;
            i < font_asset->max_glyph_count;
            ++i)
    {
        for(u32 j = 0;
                j < font_asset->max_glyph_count;
                ++j)
        {
            u32 codepoint0 = font_asset->glyph_assets[i].unicode_codepoint;
            u32 codepoint1 = font_asset->glyph_assets[j].unicode_codepoint;

            f32 kerning = load_font_info->font_scale*stbtt_GetCodepointKernAdvance(&load_font_info->font_info, codepoint0 ,codepoint1);
            font_asset->kerning_advances[i*font_asset->max_glyph_count + j] = kerning;
        }
    }

    load_font_info->font_asset = 0;
}
#endif

internal void
add_glyph_asset(LoadFontInfo *load_font_info, ThreadWorkQueue *gpu_work_queue, u32 unicode_codepoint)
{
    u16 glyphID = load_font_info->populated_glyph_count++;
    assert(load_font_info->populated_glyph_count <= load_font_info->font_asset->max_glyph_count);
    load_font_info->font_asset->codepoint_to_glyphID_table[unicode_codepoint] = glyphID;

    int x_advance_px;
    int left_bearing_px;
    stbtt_GetCodepointHMetrics(&load_font_info->font_info, unicode_codepoint, &x_advance_px, &left_bearing_px);

    GlyphAsset *glyph_asset = load_font_info->font_asset->glyph_assets + glyphID; 
    glyph_asset->unicode_codepoint = unicode_codepoint;
    glyph_asset->left_bearing_px = load_font_info->font_scale*left_bearing_px;
    glyph_asset->x_advance_px = load_font_info->font_scale*x_advance_px;
     
    int width;
    int height;
    int x_off;
    int y_off;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(&load_font_info->font_info, 
                                                    load_font_info->font_scale, load_font_info->font_scale,
                                                    unicode_codepoint,
                                                    &width, &height, &x_off, &y_off);

    // scale is already baked in
    glyph_asset->dim_px = V2(width, height);
    glyph_asset->x_offset_px = x_off;
    glyph_asset->y_offset_from_baseline_px = -(height + y_off); 

    if(bitmap)
    {
        glyph_asset->texture = load_texture_asset(gpu_work_queue, bitmap, width, height, 1);
        stbtt_FreeBitmap(bitmap, 0);
    }
}

// TODO(gh) 'stream' assets?
internal void
load_game_assets(GameAssets *assets, MemoryArena *arena, PlatformAPI *platform_api, ThreadWorkQueue *gpu_work_queue)
{
    /*
        NOTE(gh) Load font assets
    */
    u32 max_glyph_count = 2048;
    LoadFontInfo load_font_info = {};

    begin_load_font(&load_font_info, &assets->debug_font_asset, 
                    "/System/Library/Fonts/Supplemental/applemyungjo.ttf", platform_api, 
                    max_glyph_count, 128.0f);
    {
        // space works just like other glyphs, but without any texture
        add_glyph_asset(&load_font_info, gpu_work_queue, ' ');
        for(u32 codepoint = '!';
                codepoint <= '~';
                ++codepoint)
        {
            add_glyph_asset(&load_font_info, gpu_work_queue, codepoint);
        }
        add_glyph_asset(&load_font_info, gpu_work_queue, 0x8349);
        add_glyph_asset(&load_font_info, gpu_work_queue, 0x30a8);
        add_glyph_asset(&load_font_info, gpu_work_queue, 0x30f3);
        add_glyph_asset(&load_font_info, gpu_work_queue, 0x30b8);
    }
    end_load_font(&load_font_info);

    /*
        NOTE(gh) Load mesh assets
    */
    TempMemory asset_memory = start_temp_memory(arena, megabytes(128));
    GeneratedMesh sphere_mesh = generate_sphere_mesh(&asset_memory, 1.0f, 256, 128);
    load_mesh_asset(assets, gpu_work_queue, AssetTag_SphereMesh, 
                    sphere_mesh.vertices, sphere_mesh.vertex_count, 
                    sphere_mesh.indices, sphere_mesh.index_count);

    GeneratedMesh floor_mesh = generate_floor_mesh(&asset_memory, 1, 1, 0);
    load_mesh_asset(assets, gpu_work_queue, AssetTag_FloorMesh, 
                    floor_mesh.vertices, floor_mesh.vertex_count, 
                    floor_mesh.indices, floor_mesh.index_count);
    end_temp_memory(&asset_memory);
}

internal f32
get_glyph_kerning(FontAsset *font_asset, f32 scale, u32 unicode_codepoint0, u32 unicode_codepoint1)
{
    u16 glyph0ID = font_asset->codepoint_to_glyphID_table[unicode_codepoint0];
    u16 glyph1ID = font_asset->codepoint_to_glyphID_table[unicode_codepoint1];

    f32 result = scale*font_asset->kerning_advances[glyph0ID*font_asset->max_glyph_count + glyph1ID];
    return result;
}

internal f32
get_glyph_x_advance_px(FontAsset *font_asset, f32 scale, u32 unicode_codepoint)
{
    u16 glyphID = font_asset->codepoint_to_glyphID_table[unicode_codepoint];
    f32 result = scale*font_asset->glyph_assets[glyphID].x_advance_px;

    return result;
}

internal f32
get_glyph_left_bearing_px(FontAsset *font_asset, f32 scale, u32 unicode_codepoint)
{
    u16 glyphID = font_asset->codepoint_to_glyphID_table[unicode_codepoint];
    f32 result = scale*font_asset->glyph_assets[glyphID].left_bearing_px;

    return result;
}
