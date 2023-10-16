#include <stdint.h>
#include <stdlib.h>
#include "physics.h"

// Particle system
extern ParticleSystem_t ParticleSystem;

static void apply_constraints(RigidBody_t *Body)
{
	vec3 Center={ 0.0f, 0.0f, 0.0f };
	const float maxRadius=2000.0f;
	const float maxVelocity=500.0f;

	// Clamp velocity, this reduces the chance of the simulation going unstable
	Body->Velocity=Vec3_Clamp(Body->Velocity, -maxVelocity, maxVelocity);

	// Check for collision with outer boundary sphere and reflect velocity if needed
	vec3 Normal=Vec3_Subv(Body->Position, Center);

	float distanceSq=Vec3_Dot(Normal, Normal);

	if(distanceSq>maxRadius*maxRadius)
	{
		float Distance=rsqrtf(distanceSq);
		// Normalize the normal
		Normal=Vec3_Muls(Normal, Distance);

		// Simple velocity reflection to bounce off the "wall"
		Body->Velocity=Vec3_Reflect(Normal, Body->Velocity);
	}
}

void PhysicsIntegrate(RigidBody_t *body, float dt)
{
	const float damping=0.01f;

	// Clamp delta time, if it's longer than 16MS, clamp it to that.
	//   This reduces the chance of the simulation going unstable.
	if(dt>0.016f)
		dt=0.016f;

	// Apply damping force
	// Force+=Velocity*-damping
	body->Force=Vec3_Addv(body->Force, Vec3_Muls(body->Velocity, -damping));

	// Euler integration of position and velocity
	// Position+=Velocity*dt+0.5f*Force/Mass*dt*dt
	// Velocity+=Force/Mass*dt
	float massDeltaTimeSq=0.5f*body->invMass*dt*dt;

	body->Position=Vec3_Addv(body->Position, Vec3_Addv(Vec3_Muls(body->Velocity, dt), Vec3_Muls(body->Force, massDeltaTimeSq)));

	body->Velocity=Vec3_Addv(body->Velocity, Vec3_Muls(body->Force, body->invMass*dt));

	body->Force=Vec3b(0.0f);

	apply_constraints(body);
}

void PhysicsExplode(RigidBody_t *Body)
{
	vec3 explosion_center={ 0.0f, 0.0f, 0.0f };

	// Calculate direction from explosion center to fragment
	vec3 direction=Vec3_Subv(Body->Position, explosion_center);
	Vec3_Normalize(&direction);

	// Calculate acceleration and impulse force
	vec3 acceleration=Vec3_Muls(direction, EXPLOSION_POWER);

	// F=M*A bla bla...
	vec3 force=Vec3_Muls(acceleration, Body->Mass);

	// Add it into object's velocity
	Body->Velocity=Vec3_Addv(Body->Velocity, force);
}

void PhysicsSphereToSphereCollisionResponse(RigidBody_t *a, RigidBody_t *b)
{
	// Calculate the distance between the camera and the sphere's center
	vec3 Normal=Vec3_Subv(b->Position, a->Position);

	const float DistanceSq=Vec3_Dot(Normal, Normal);

	// Sum of radii
	const float radiusSum=a->Radius+b->Radius;

	// Check if the distance is less than the sum of the radii
	if(DistanceSq<radiusSum*radiusSum&&DistanceSq)
	{
		float distance=sqrtf(DistanceSq);

		Normal=Vec3_Muls(Normal, 1.0f/distance);

		const float Penetration=radiusSum-distance;
		vec3 positionImpulse=Vec3_Muls(Normal, Penetration*0.5f);

		a->Position=Vec3_Subv(a->Position, positionImpulse);
		b->Position=Vec3_Addv(b->Position, positionImpulse);

		vec3 contactVelocity=Vec3_Subv(b->Velocity, a->Velocity);

		const float totalMass=a->invMass+b->invMass;
		const float Restitution=0.66f;
		const float VdotN=Vec3_Dot(contactVelocity, Normal);
		float j=(-(1.0f+Restitution)*VdotN)/totalMass;

		a->Velocity=Vec3_Subv(a->Velocity, Vec3_Muls(Normal, j*a->invMass));
		b->Velocity=Vec3_Addv(b->Velocity, Vec3_Muls(Normal, j*b->invMass));
	}
}

void PhysicsSphereToAABBCollisionResponse(RigidBody_t *sphere, RigidBody_t *aabb)
{
	// Calculate the half extents of the AABB
	vec3 half=Vec3_Muls(aabb->Size, 0.5f);

	// Calculate the AABB's min and max points
	vec3 aabbMin=Vec3_Subv(aabb->Position, half);
	vec3 aabbMax=Vec3_Addv(aabb->Position, half);

	// Find the closest point on the AABB to the sphere
	vec3 closest=Vec3(
		fmaxf(aabbMin.x, fminf(sphere->Position.x, aabbMax.x)),
		fmaxf(aabbMin.y, fminf(sphere->Position.y, aabbMax.y)),
		fmaxf(aabbMin.z, fminf(sphere->Position.z, aabbMax.z))
	);

	// Calculate the distance between the closest point and the sphere's center
	vec3 Normal=Vec3_Subv(closest, sphere->Position);

	float DistanceSq=Vec3_Dot(Normal, Normal);

	// Check if the distance is less than the sphere's radius
	if(DistanceSq<=sphere->Radius*sphere->Radius&&DistanceSq)
	{
		float distance=sqrtf(DistanceSq);

		Normal=Vec3_Muls(Normal, 1.0f/distance);

		const float Penetration=sphere->Radius-distance;
		vec3 positionImpulse=Vec3_Muls(Normal, Penetration*0.5f);

		sphere->Position=Vec3_Subv(sphere->Position, positionImpulse);
		aabb->Position=Vec3_Addv(aabb->Position, positionImpulse);

		vec3 contactVelocity=Vec3_Subv(aabb->Velocity, sphere->Velocity);

		const float totalMass=sphere->invMass+aabb->invMass;
		const float Restitution=0.6f;
		const float VdotN=Vec3_Dot(contactVelocity, Normal);
		float j=(-(1.0f+Restitution)*VdotN)/totalMass;

		sphere->Velocity=Vec3_Subv(sphere->Velocity, Vec3_Muls(Normal, j*sphere->invMass));
		aabb->Velocity=Vec3_Addv(aabb->Velocity, Vec3_Muls(Normal, j*aabb->invMass));
	}
}

// Camera<->Rigid body collision detection and response
void PhysicsCameraToSphereCollisionResponse(Camera_t *Camera, RigidBody_t *Body)
{
	// Camera mass constants, since camera struct doesn't store these
	const float Camera_Mass=100.0f;
	const float Camera_invMass=1.0f/Camera_Mass;

	// Calculate the distance between the camera and the sphere's center
	vec3 Normal=Vec3_Subv(Body->Position, Camera->Position);

	float DistanceSq=Vec3_Dot(Normal, Normal);

	// Sum of radii
	float radiusSum=Camera->Radius+Body->Radius;

	// Check if the distance is less than the sum of the radii
	if(DistanceSq<=radiusSum*radiusSum&&DistanceSq)
	{
		float distance=sqrtf(DistanceSq);

		Normal=Vec3_Muls(Normal, 1.0f/distance);

		const float Penetration=radiusSum-distance;
		vec3 positionImpulse=Vec3_Muls(Normal, Penetration*0.5f);

		Camera->Position=Vec3_Subv(Camera->Position, positionImpulse);
		Body->Position=Vec3_Addv(Body->Position, positionImpulse);

		vec3 contactVelocity=Vec3_Subv(Body->Velocity, Camera->Velocity);

		const float totalMass=Camera_invMass+Body->invMass;
		const float Restitution=0.66f;
		const float VdotN=Vec3_Dot(contactVelocity, Normal);
		float j=(-(1.0f+Restitution)*VdotN)/totalMass;

		Camera->Velocity=Vec3_Subv(Camera->Velocity, Vec3_Muls(Normal, j*Camera_invMass));
		Body->Velocity=Vec3_Addv(Body->Velocity, Vec3_Muls(Normal, j*Body->invMass));

		const float relVelMag=sqrtf(fabsf(VdotN));
	}
}

void PhysicsCameraToCameraCollisionResponse(Camera_t *CameraA, Camera_t *CameraB)
{
	// Camera mass constants, since camera struct doesn't store these
	const float Camera_Mass=100.0f;
	const float Camera_invMass=1.0f/Camera_Mass;

	// Calculate the distance between the camera and the sphere's center
	vec3 Normal=Vec3_Subv(CameraB->Position, CameraA->Position);

	float DistanceSq=Vec3_Dot(Normal, Normal);

	// Sum of radii
	float radiusSum=CameraA->Radius+CameraB->Radius;

	// Check if the distance is less than the sum of the radii
	if(DistanceSq<=radiusSum*radiusSum&&DistanceSq)
	{
		float distance=sqrtf(DistanceSq);

		Normal=Vec3_Muls(Normal, 1.0f/distance);

		const float Penetration=radiusSum-distance;
		vec3 positionImpulse=Vec3_Muls(Normal, Penetration*0.5f);

		CameraA->Position=Vec3_Subv(CameraA->Position, positionImpulse);
		CameraB->Position=Vec3_Addv(CameraB->Position, positionImpulse);

		vec3 contactVelocity=Vec3_Subv(CameraB->Velocity, CameraA->Velocity);

		const float totalMass=Camera_invMass+Camera_invMass;
		const float Restitution=0.66f;
		const float VdotN=Vec3_Dot(contactVelocity, Normal);
		float j=(-(1.0f+Restitution)*VdotN)/totalMass;

		CameraA->Velocity=Vec3_Subv(CameraA->Velocity, Vec3_Muls(Normal, j*Camera_invMass));
		CameraB->Velocity=Vec3_Addv(CameraB->Velocity, Vec3_Muls(Normal, j*Camera_invMass));

		const float relVelMag=sqrtf(fabsf(VdotN));
	}
}

// Particle<->Rigid body collision detection and response
void PhysicsParticleToSphereCollisionResponse(Particle_t *Particle, RigidBody_t *Body)
{
	// Particle constants, since particle struct doesn't store these
	const float Particle_Radius=2.0f;
	const float Particle_Mass=1.0f;
	const float Particle_invMass=1.0f/Particle_Mass;

	// Calculate the distance between the particle and the sphere's center
	vec3 Normal=Vec3_Subv(Body->Position, Particle->pos);

	float DistanceSq=Vec3_Dot(Normal, Normal);

	// Sum of radii
	float radiusSum=Particle_Radius+Body->Radius;

	// Check if the distance is less than the sum of the radii
	if(DistanceSq<=radiusSum*radiusSum&&DistanceSq)
	{
		float distance=sqrtf(DistanceSq);

		Normal=Vec3_Muls(Normal, 1.0f/distance);

		const float Penetration=radiusSum-distance;
		vec3 positionImpulse=Vec3_Muls(Normal, Penetration*0.5f);

		Particle->pos=Vec3_Subv(Particle->pos, positionImpulse);
		Body->Position=Vec3_Addv(Body->Position, positionImpulse);

		vec3 contactVelocity=Vec3_Subv(Body->Velocity, Particle->vel);

		const float totalMass=Particle_invMass+Body->invMass;
		const float Restitution=0.66f;
		const float VdotN=Vec3_Dot(contactVelocity, Normal);
		float j=(-(1.0f+Restitution)*VdotN)/totalMass;

		Particle->vel=Vec3_Subv(Particle->vel, Vec3_Muls(Normal, j*Particle_invMass));
		Body->Velocity=Vec3_Addv(Body->Velocity, Vec3_Muls(Normal, j*Body->invMass));
	}
}
