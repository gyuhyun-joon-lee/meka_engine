/*
 * Written by Gyuhyun Lee
 */

#include "hb_render.h"

internal Camera
init_fps_camera(v3 p, f32 focal_length, f32 fov_in_degree, f32 near, f32 far)
{
    Camera result = {};

    result.p = p;
    result.focal_length = focal_length;

    result.fov = degree_to_radian(fov_in_degree);
    result.near = near;
    result.far = far;

    result.pitch = 0.0f;
    result.yaw = 0.0f;
    result.roll = 0.0f;

    return result;
}

internal CircleCamera
init_circle_camera(v3 p, v3 lookat_p, f32 distance_from_axis, f32 fov_in_degree, f32 near, f32 far)
{
    CircleCamera result = {};

    result.p = p;
    // TODO(gh) we are not allowing arbitrary point for this type of camera, for now.
    assert(p.x == 0.0f && p.y == 0);
    result.lookat_p = lookat_p;
    result.distance_from_axis = distance_from_axis;
    result.rad = 0;

    result.fov = degree_to_radian(fov_in_degree);
    result.near = near;
    result.far = far;

    return result;
}

/* 
   NOTE(gh) Rotation matrix
   Let's say that we have a tranform matrix that looks like this
   |xa ya za|
   |xb yb zb|
   |xc yc zc|

   This means that whenever we multiply a point with this, we are projecting the point into a coordinate system
   with three axes : [xa ya za], [xb yb zb], [xc yc zc] by taking a dot product.
   Common example for this would be world to camera transform matrix.

   Let's now setup the matrix in the other way by transposing the matrix above. 
   |xa xb xc|
   |ya yb yc|
   |za zb zc|
   Whenever we multiply a point by this, the result would be p.x*[xa ya za] + p.y*[xb yb zb] + p.z*[xc yc zc]
   Common example for this would be camera to world matrix, where we have a point in camera space and 
   we want to get what the p would be in world coordinates 

   As you can see, two matrix is doing the exact opposite thing, which means they are inverse matrix to each other.
   This make sense, as the rotation matrix is an orthogonal matrix(both rows and the columns are unit vectors),
   so that the inverse = transpose
*/

// NOTE(gh) To move the world space position into camera space,
// 1. We first translate the position so that the origin of the camera space
// in world coordinate is (0, 0, 0). We can achieve this by  simply subtracing the camera position
// from the world position.
// 2. We then project the translated position into the 3 camera axes. This is done simply by using a dot product.
//
// To pack this into a single matrix, we need to multiply the translation by the camera axis matrix,
// because otherwise it will be projection first and then translation -> which is completely different from
// doing the translation and the projection.
internal m4x4 
camera_transform(v3 camera_p, v3 camera_x_axis, v3 camera_y_axis, v3 camera_z_axis)
{
    TIMED_BLOCK();
    m4x4 result = {};

    // NOTE(gh) to pack the rotation & translation into one matrix(with an order of translation and the rotation),
    // we need to first multiply the translation by the rotation matrix
    v3 multiplied_translation = V3(dot(camera_x_axis, -camera_p), 
                                    dot(camera_y_axis, -camera_p),
                                    dot(camera_z_axis, -camera_p));

    result.rows[0] = V4(camera_x_axis, multiplied_translation.x);
    result.rows[1] = V4(camera_y_axis, multiplied_translation.y);
    result.rows[2] = V4(camera_z_axis, multiplied_translation.z);
    result.rows[3] = V4(0.0f, 0.0f, 0.0f, 1.0f); // Dont need to touch the w part, view matrix doesn't produce homogeneous coordinates

    return result;
}

internal m4x4
camera_transform(Camera *camera)
{
    // NOTE(gh) FPS camear removes one axis to avoid gimbal lock. 
    m3x3 camera_local_rotation = z_rotate(camera->roll) * x_rotate(camera->pitch);
    
    // NOTE(gh) camera aligns with the world coordinate in default.
    v3 camera_x_axis = normalize(camera_local_rotation * V3(1, 0, 0));
    v3 camera_y_axis = normalize(camera_local_rotation * V3(0, 1, 0));
    v3 camera_z_axis = normalize(camera_local_rotation * V3(0, 0, 1));
    m3x3 transpose_camera_local_rotation = transpose(camera_local_rotation);

    return camera_transform(camera->p, camera_x_axis, camera_y_axis, camera_z_axis);
}

internal m4x4
camera_transform(CircleCamera *camera)
{
    // -z is the looking direction
    v3 camera_z_axis = -normalize(camera->lookat_p - camera->p);

    // TODO(gh) This does not work if the camera was direction looking down or up in world z axis
    assert(!(camera_z_axis.x == 0.0f && camera_z_axis.y == 0.0f));
    v3 camera_x_axis = normalize(cross(V3(0, 0, 1), camera_z_axis));
    v3 camera_y_axis = normalize(cross(camera_z_axis, camera_x_axis));

    return camera_transform(camera->p, camera_x_axis, camera_y_axis, camera_z_axis);
}

// NOTE(gh) persepctive projection matrix for (-1, -1, 0) to (1, 1, 1) NDC like Metal
/*
    Little tip in how we get the persepctive projection matrix
    Think as 2D plane(x and z OR y and z), use triangle similarity to get projected Xp and Yp
    For Z, as x and y don't have any effect on Z, we can say (A * Ze + b) / -Ze = Zp (diving by -Ze because homogeneous coords)

    -n should produce 0 or -1 value(based on what NDC system we use), 
    while -f should produce 1.
*/
inline m4x4
perspective_projection_near_is_01(f32 fov, f32 n, f32 f, f32 width_over_height)
{
    assert(fov < 180);

    f32 half_near_plane_width = n*tanf(0.5f*fov)*0.5f;
    f32 half_near_plane_height = half_near_plane_width / width_over_height;

    m4x4 result = {};

    // TODO(gh) Even though the resulting coordinates should be same, 
    // it seems like in Metal w value should be positive.
    // Maybe that's how they are doing the frustum culling..?
    result.rows[0] = V4(n / half_near_plane_width, 0, 0, 0);
    result.rows[1] = V4(0, n / half_near_plane_height, 0, 0);
    result.rows[2] = V4(0, 0, f/(n-f), (n*f)/(n-f)); // X and Y values don't effect the z value
    result.rows[3] = V4(0, 0, -1, 0); // As Xp and Yp are dependant to Z value, this is the only way to divide Xp and Yp by -Ze

    return result;
}


internal v3
get_camera_lookat(Camera *camera)
{
    // TODO(gh) I don't think the math here is correct, shouldn't we do this in backwards 
    // to go from camera space to world space, considering that (0, 0, -1) is the camera lookat in
    // camera space??
    m3x3 camera_local_rotation = z_rotate(camera->roll) * x_rotate(camera->pitch);
    v3 result = camera_local_rotation * V3(0, 0, -1); 

    return result;
}

// TODO(gh) Does not work when camera is directly looking up or down
internal v3
get_camera_right(Camera *camera)
{
    // TODO(gh) Up vector might be same as the camera direction
    v3 camera_dir = get_camera_lookat(camera);
    v3 result = normalize(cross(camera_dir, V3(0, 0, 1)));

    return result;
}

// TODO(gh) This operation is quite expensive, find out to optimize it
internal void
get_camera_frustum(Camera *camera, CameraFrustum *frustum, f32 width_over_height)
{
    v3 camera_dir = get_camera_lookat(camera);
    v3 camera_right = get_camera_right(camera);
    v3 camera_up = normalize(cross(camera_right, camera_dir));

    v3 near_plane_center = camera->p + camera->near * camera_dir;
    v3 far_plane_center = camera->p + camera->far * camera_dir;

    f32 half_near_plane_width = camera->near*tanf(0.5f*camera->fov)*0.5f;
    f32 half_near_plane_height = half_near_plane_width / width_over_height;
    v3 half_near_plane_right = half_near_plane_width * camera_right;
    v3 half_near_plane_up = half_near_plane_height * camera_up;

    f32 half_far_plane_width = camera->far*tanf(0.5f*camera->fov)*0.5f;
    f32 half_far_plane_height = half_far_plane_width / width_over_height;
    v3 half_far_plane_right = half_far_plane_width * camera_right;
    v3 half_far_plane_up = half_far_plane_height * camera_up;

    // morten z order
    frustum->near[0] = near_plane_center - half_near_plane_right + half_near_plane_up;
    frustum->near[1] = near_plane_center + half_near_plane_right + half_near_plane_up;
    frustum->near[2] = near_plane_center - half_near_plane_right - half_near_plane_up;
    frustum->near[3] = near_plane_center + half_near_plane_right - half_near_plane_up;

    frustum->far[0] = far_plane_center - half_far_plane_right + half_far_plane_up;
    frustum->far[1] = far_plane_center + half_far_plane_right + half_far_plane_up;
    frustum->far[2] = far_plane_center - half_far_plane_right - half_far_plane_up;
    frustum->far[3] = far_plane_center + half_far_plane_right - half_far_plane_up;
}

internal m4x4
rhs_to_lhs(m4x4 m)
{
    m4x4 result = m;
    result.rows[2] *= -1.0f;

    return result;
}

// TODO(gh) Later we would want to minimize passing the platform buffer here
// TODO(gh) pass center & dim, instead of min & max?
internal void
init_grass_grid(PlatformRenderPushBuffer *render_push_buffer, Entity *floor, RandomSeries *series, 
                GrassGrid *grass_grid, u32 grass_count_x, u32 grass_count_y, v2 min, v2 max)
{
    grass_grid->grass_count_x = grass_count_x;
    grass_grid->grass_count_y = grass_count_y;
    grass_grid->updated_floor_z_buffer = false;
    grass_grid->min = min;
    grass_grid->max = max;

    u32 total_grass_count = grass_grid->grass_count_x * grass_grid->grass_count_y;

    if(!grass_grid->floor_z_buffer)
    {
        grass_grid->floor_z_buffer = (f32 *)((u8 *)render_push_buffer->giant_buffer + render_push_buffer->giant_buffer_used);
        grass_grid->floor_z_buffer_size = sizeof(f32) * total_grass_count;
        grass_grid->floor_z_buffer_offset = render_push_buffer->giant_buffer_used;

        render_push_buffer->giant_buffer_used += grass_grid->floor_z_buffer_size;
        assert(render_push_buffer->giant_buffer_used <= render_push_buffer->giant_buffer_size);

        // TODO(gh) temp code, need to do proper raycast later
        for(u32 grass_index = 0;
                grass_index < grass_count_x * grass_count_y;
                ++grass_index)
        {
            *(f32 *)grass_grid->floor_z_buffer = 0;
        }

        grass_grid->updated_floor_z_buffer = true;
    }

    if(!grass_grid->perlin_noise_buffer)
    {
        grass_grid->perlin_noise_buffer = (f32 *)((u8 *)render_push_buffer->giant_buffer + render_push_buffer->giant_buffer_used);
        grass_grid->perlin_noise_buffer_size = sizeof(f32) * total_grass_count;
        grass_grid->perlin_noise_buffer_offset = render_push_buffer->giant_buffer_used;

        render_push_buffer->giant_buffer_used += grass_grid->perlin_noise_buffer_size;
        assert(render_push_buffer->giant_buffer_used <= render_push_buffer->giant_buffer_size);
    }
}

internal void
init_render_push_buffer(PlatformRenderPushBuffer *render_push_buffer, Camera *render_camera, Camera *game_camera,  
                        GrassGrid *grass_grids, u32 grass_grid_count_x, u32 grass_grid_count_y,
                        v3 clear_color, b32 enable_shadow)
{
    TIMED_BLOCK();
    assert(render_push_buffer->base);

    render_push_buffer->render_camera_view = camera_transform(render_camera);
    render_push_buffer->render_camera_near = render_camera->near;
    render_push_buffer->render_camera_far = render_camera->far;
    render_push_buffer->render_camera_fov = render_camera->fov;
    render_push_buffer->render_camera_p = render_camera->p;

    render_push_buffer->game_camera_view = camera_transform(game_camera);
    render_push_buffer->game_camera_near = game_camera->near;
    render_push_buffer->game_camera_far = game_camera->far;
    render_push_buffer->game_camera_fov = game_camera->fov;
    render_push_buffer->game_camera_p = game_camera->p;

    render_push_buffer->clear_color = clear_color;
    render_push_buffer->grass_grids = grass_grids;
    render_push_buffer->grass_grid_count_x = grass_grid_count_x;
    render_push_buffer->grass_grid_count_y = grass_grid_count_y;

    render_push_buffer->enable_shadow = enable_shadow;
    
    render_push_buffer->combined_vertex_buffer_used = 0;
    render_push_buffer->combined_index_buffer_used = 0;

    render_push_buffer->used = 0;
}

internal void *
_push_render_element(PlatformRenderPushBuffer *render_push_buffer, u32 size)
{
    void *entry = (void *)(render_push_buffer->base + render_push_buffer->used);

    render_push_buffer->used += size;
    assert(render_push_buffer->used <= render_push_buffer->total_size);

    return entry;
}
#define push_render_element(render_push_buffer, type) (type *)_push_render_element(render_push_buffer, sizeof(type))

// TODO(gh) do we want to collape this to single line_group or something to save memory(color, type)?
internal void
push_line(PlatformRenderPushBuffer *render_push_buffer, v3 start, v3 end, v3 color)
{
    TIMED_BLOCK();

    RenderEntryLine *entry = push_render_element(render_push_buffer, RenderEntryLine);

    entry->header.type = RenderEntryType_Line;
    entry->header.size = sizeof(*entry);

    entry->start = start;
    entry->end = end;
    entry->color = color;
}

internal u32
push_data(void *dst_buffer, u64 *dst_used, u64 dst_size, void *src, u32 src_size)
{
    TIMED_BLOCK();
    u32 original_used = *dst_used;

    void *dst = (u8 *)dst_buffer + original_used;
    memcpy(dst, src, src_size);

    *dst_used += src_size;
    assert(*dst_used <= dst_size);

    return original_used;
}

internal void
push_frustum(PlatformRenderPushBuffer *render_push_buffer, v3 color, 
            v3 *vertices, u32 vertex_count, u32 *indices, u32 index_count)
{
    RenderEntryFrustum *entry = push_render_element(render_push_buffer, RenderEntryFrustum);

    entry->header.type = RenderEntryType_Frustum;
    entry->header.size = sizeof(*entry);

    entry->color = color;

    entry->vertex_buffer_offset = push_data(render_push_buffer->combined_vertex_buffer, 
                                            &render_push_buffer->combined_vertex_buffer_used, 
                                            render_push_buffer->combined_vertex_buffer_size,
                                            vertices, sizeof(vertices[0]) * vertex_count);

    entry->index_buffer_offset = push_data(render_push_buffer->combined_index_buffer, 
                                            &render_push_buffer->combined_index_buffer_used, 
                                            render_push_buffer->combined_index_buffer_size,
                                            indices, sizeof(indices[0]) * index_count);
    entry->index_count = index_count;
}

internal void
push_mesh_pn(PlatformRenderPushBuffer *render_push_buffer, v3 p, v3 dim, v3 color, 
            GameAssets *asset, PlatformAPI *platform_api, 
          VertexPN *vertices, u32 vertex_count, u32 *indices, u32 index_count, 
          u32 *mesh_assetID, b32 should_cast_shadow)
{
    TIMED_BLOCK();
    RenderEntryMeshPN *entry = push_render_element(render_push_buffer, RenderEntryMeshPN);

    entry->header.type = RenderEntryType_MeshPN;
    entry->header.size = sizeof(*entry);

    // TODO(gh) entity should not have vertices directly, the asset system should be able to 
    // get the vertex and index information using tag and match vector
    MeshAsset *mesh_asset = get_mesh_asset(asset, platform_api, render_push_buffer->device, mesh_assetID, 
                                            vertices, vertex_count, indices, index_count);
    entry->vertex_buffer_handle = mesh_asset->vertex_buffer_handle;
    entry->vertex_count = mesh_asset->vertex_count;
    entry->index_buffer_handle = mesh_asset->index_buffer_handle;
    entry->index_count = mesh_asset->index_count;

    entry->p = p;
    entry->dim = dim;

    entry->should_cast_shadow = should_cast_shadow;
    
    entry->color = color;
}

// TODO(gh) Change this with textured quad? Because we have to have some kind of texture system
// that is visible from the game code someday!
internal void
push_glyph(PlatformRenderPushBuffer *render_push_buffer, FontAsset *font_asset, v3 color, 
            v2 top_left_rel_p_px, u32 codepoint, f32 scale)
{
    RenderEntryGlyph *entry = push_render_element(render_push_buffer, RenderEntryGlyph);

    entry->header.type = RenderEntryType_Glyph;
    entry->header.size = sizeof(*entry);

    u32 glyphID = font_asset->codepoint_to_glyphID_table[codepoint];

    GlyphAsset *glyph_asset = font_asset->glyph_assets + glyphID;
    assert(glyph_asset->texture.handle);
    entry->texture_handle = glyph_asset->texture.handle;
    entry->color = color;

    // TODO(gh) Do we wanna pull this out?
    v2 bottom_left_rel_p_px = V2(top_left_rel_p_px.x, render_push_buffer->window_height - top_left_rel_p_px.y);
    // TODO(gh) also document this sorcery
    v2 min_px = bottom_left_rel_p_px + scale*(V2(0, -font_asset->ascent_from_baseline) + 
                                                V2(0, glyph_asset->y_offset_from_baseline_px));
    
    v2 max_px = min_px + scale*glyph_asset->dim_px;

    entry->min = 2*V2(min_px.x / render_push_buffer->window_width, min_px.y / render_push_buffer->window_height) - V2(1, 1);
    entry->max = 2*V2(max_px.x / render_push_buffer->window_width, max_px.y / render_push_buffer->window_height) - V2(1, 1);

    entry->texcoord_min = V2(0, 0);
    entry->texcoord_max = V2(1, 1);
}

// NOTE(gh) This is NOT the recommended way to draw things, use push_mesh_pn for larger meshes!
internal void
push_arbitrary_mesh(PlatformRenderPushBuffer *render_push_buffer, v3 color, VertexPN *vertices, u32 vertex_count, u32 *indices, u32 index_count)
{
    RenderEntryArbitraryMesh *entry = push_render_element(render_push_buffer, RenderEntryArbitraryMesh);
    entry->header.type = RenderEntryType_ArbitraryMesh;
    entry->header.size = sizeof(*entry);

    entry->color = color;

    entry->vertex_buffer_offset = push_data(render_push_buffer->combined_vertex_buffer, 
                                            &render_push_buffer->combined_vertex_buffer_used, 
                                            render_push_buffer->combined_vertex_buffer_size,
                                            vertices, sizeof(vertices[0]) * vertex_count);

    entry->index_buffer_offset = push_data(render_push_buffer->combined_index_buffer, 
                                            &render_push_buffer->combined_index_buffer_used, 
                                            render_push_buffer->combined_index_buffer_size,
                                            indices, sizeof(indices[0]) * index_count);
    entry->index_count = index_count; 
}













