#ifndef HB_PBD_H
#define HB_PBD_H

/*
    NOTE(gh) Some equations involved in PBD
    lagrange multiplier(lambda) = C(p) / sum(abs(gradient))
*/

// TODO(gh) Instead of this AOS, use SOA
struct PBDParticle
{
    v3d p;
    v3d v;

    //  TODO(gh) For now, all particles have identical size(radius) to avoid clipping,
    // but there might be workaround for this!
    f32 r;
    f64 inv_mass;
    v3d initial_offset_from_com; // This is the initial offset

    // NOTE(gh) Used for grouping particles. i.e particles in the same object would have the same phase, 
    // preventing them from colliding each other
    i32 phase; 

    /*
        NOTE(gh) Temporary varaibles, should be cleared to 0 each frame
    */
    // TODO(gh) Might not be necessary(i.e don't store velocity, and get it implicitly each frame?)
    v3d prev_p;
    v3 d_p_sum;
    u32 constraint_hit_count;
};


struct PBDParticlePool
{
    // TODO(gh) Probably not a good idea...
    PBDParticle particles[4096];
    u32 count;
};


struct FixedPositionConstraint
{
    u32 index;
    v3 fixed_p; // world space
};

/*
    NOTE(gh) Collision Constraint between two particles
    C(x0, x1) = |x0 −x1|−(r0 +r1) ≥ 0 
*/
struct CollisionConstraint
{
    PBDParticle *particle0;
    PBDParticle *particle1;
};

/*
    NOTE(gh) Environment collision constraint between one particle and the environment
    C(x) = dot(plane_normal, particle_position) − plane_d - radius ≥ 0
*/
struct EnvironmentConstraint
{
    PBDParticle *particle;

    // environment info
    v3d n; // should be normalized
    // Should also include the radius of the particle, too
    f32 d;
};

/*
    NOTE(gh) Distance constraint between two particles
    C = distance_between(x0, x1) - rest_length;

    gradient_c(x0) = normalize(x1 - x0); // so basically pull x0 towards x1
    gradient_c(x1) = normalize(x0 - x1); // so basically pull x1 towards x0

    lagrange_multiplier = -C / (w0 + w1 + alpha/dt^2),
    where w0 and w1 are the inverse mass of the particles,
    and alpha = 1/stiffness
*/
struct DistanceConstraint
{
    u32 index0;
    u32 index1;

    f32 rest_length;
};

/*
    0 is the top vertex,
    1, 2, 3 forms the bottom triangle in counter-clockwise order, 

    gradient(x0) = (x2-x1) x (x3 - x1);
    gradient(x1) = (x0-x2) x (x3 - x2);
    gradient(x2) = (x3-x1) x (x0 - x1);
    gradient(x3) = (x1-x2) x (x0 - x2);
*/
struct VolumeConstraint
{
    u32 index0; // top
    u32 index1; // bottom0
    u32 index2; // bottom1
    u32 index3; // bottom2

    f32 rest_volume;
};

struct PBDParticleGroup
{
    // particles should be laid out sequentially
    PBDParticle *particles;
    u32 count;

    DistanceConstraint *distance_constraints;
    u32 distance_constraint_count;
    f32 inv_distance_stiffness;

    VolumeConstraint *volume_constraints;
    u32 volume_constraint_count;
    // f32 inv_volume_stiffness;
};















#endif