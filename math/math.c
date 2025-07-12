#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <float.h>
#include "math.h"

// Some fast approx. trig. functions
float fsinf(const float v)
{
	float fx=v*0.1591549f+0.5f;
	float ix=fx-(float)floor(fx);
	float x=ix*6.2831852f-3.1415926f;
	float x2=x*x;
	float x3=x2*x;

	return x-x3/6.0f
			+x3*x2/120.0f
			-x3*x2*x2/5040.0f
			+x3*x2*x2*x2/362880.0f
			-x3*x2*x2*x2*x2/39916800.0f
			+x3*x2*x2*x2*x2*x2/6227020800.0f
			-x3*x2*x2*x2*x2*x2*x2/1307674279936.0f
			+x3*x2*x2*x2*x2*x2*x2*x2/355687414628352.0f;
}

float fcosf(const float v)
{
	float fx=v*0.1591549f+0.5f;
	float ix=fx-(float)floor(fx);
	float x=ix*6.2831852f-3.1415926f;
	float x2=x*x;
	float x4=x2*x2;

	return 1-x2/2.0f
			+x4/24.0f
			-x4*x2/720.0f
			+x4*x4/40320.0f
			-x4*x4*x2/3628800.0f
			+x4*x4*x4/479001600.0f
			-x4*x4*x4*x2/87178289152.0f
			+x4*x4*x4*x4/20922788478976.0f;
}

float ftanf(const float x)
{
	return fsinf(x)/fcosf(x);
}

// Bit-fiddle fast reciprocal square root, ala Quake 3
float rsqrtf(float x)
{
	long i;
	float x2=x*0.5f, y=x;
	const float threehalfs=1.5f;

	i=*(long *)&y;				// evil floating point bit level hacking
	i=0x5F3759DF-(i>>1);		// WTF? 
	y=*(float *)&i;
	y=y*(threehalfs-(x2*y*y));	// 1st iteration
//	y=y*(threehalfs-(x2*y*y));	// 2nd iteration, this can be removed

	return y;
}

// Misc functions
float fact(const int32_t n)
{
	int32_t i;
	float j=1.0f;

	for(i=1;i<n;i++)
		j*=i;

	return j;
}

static uint32_t randomSeed=0;

void RandomSeed(uint32_t seed)
{
	randomSeed=seed;
}

uint32_t Random(void)
{
#if 0
	// Wang
	randomSeed=((randomSeed^61u)^(randomSeed>>16u))*9u;
	randomSeed=(randomSeed^(randomSeed>>4u))*0x27d4EB2Du;
	randomSeed=randomSeed^(randomSeed>>15u);
#else
	// PCG
	uint32_t State=randomSeed*0x2C9277B5u+0xAC564B05u;
	uint32_t Word=((State>>((State>>28u)+4u))^State)*0x108EF2D9u;
	randomSeed=(Word>>22u)^Word;
#endif

	return randomSeed;
}

int32_t RandRange(int32_t min, int32_t max)
{
	return (Random()%(max-min+1))+min;
}

float RandFloat(void)
{
	return (float)Random()/(float)UINT32_MAX;
}

float RandFloatRange(float min, float max)
{
	return ((max-min)*RandFloat())+min;
}

uint32_t IsPower2(uint32_t value)
{
	return (!!value)&!((value+~1+1)&value);
}

uint32_t NextPower2(uint32_t value)
{
	value--;
	value|=value>>1;
	value|=value>>2;
	value|=value>>4;
	value|=value>>8;
	value|=value>>16;
	value++;

	return value;
}

int32_t ComputeLog(uint32_t value)
{
	int32_t i=0;

	if(value==0)
		return -1;

	for(;;)
	{
		if(value&1)
		{
			if(value!=1)
				return -1;

			return i;
		}

		value>>=1;
		i++;
	}
}

float Lerp(const float a, const float b, const float t)
{
	return t*(b-a)+a;
}

float rayOBBIntersect(const vec3 rayOrigin, const vec3 rayDirection, const vec3 obbCenter, const vec3 obbHalfSize, const vec4 obbOrientation)
{
	// Transform ray into OBB local space using inverse rotation
	const vec4 invOrientation=QuatInverse(obbOrientation);
	const vec3 localRayOrigin=QuatRotate(invOrientation, Vec3_Subv(rayOrigin, obbCenter));
	const vec3 localRayDir=QuatRotate(invOrientation, rayDirection);

	// Compute intersection of ray with all six bbox planes
	const vec3 invR=Vec3(1.0f/localRayDir.x, 1.0f/localRayDir.y, 1.0f/localRayDir.z);

	const vec3 tbot=Vec3_Mulv(Vec3_Subv(Vec3_Muls(obbHalfSize, -1.0f), localRayOrigin), invR);
	const vec3 ttop=Vec3_Mulv(Vec3_Subv(Vec3_Muls(obbHalfSize, 1.0f), localRayOrigin), invR);

	// Reorder intersections to find smallest and largest on each axis
	const vec3 tmin=Vec3(fminf(ttop.x, tbot.x), fminf(ttop.y, tbot.y), fminf(ttop.z, tbot.z));
	const vec3 tmax=Vec3(fmaxf(ttop.x, tbot.x), fmaxf(ttop.y, tbot.y), fmaxf(ttop.z, tbot.z));

	// Find the largest tmin and the smallest tmax
	const float t0=fmaxf(fmaxf(tmin.x, tmin.y), tmin.z);
	const float t1=fminf(fminf(tmax.x, tmax.y), tmax.z);

	if(t0>t1||t1<0.0f)
		return -1.0f;

	return t0;
}

float raySphereIntersect(const vec3 rayOrigin, const vec3 rayDirection, const vec3 sphereCenter, const float sphereRadius)
{
	const vec3 oc=Vec3_Subv(rayOrigin, sphereCenter);
	const float a=Vec3_Dot(rayDirection, rayDirection);
	const float b=2.0f*Vec3_Dot(oc, rayDirection);
	const float c=Vec3_Dot(oc, oc)-sphereRadius*sphereRadius;
	const float discriminant=b*b-4*a*c;

	if(discriminant<0.0f)
		return -1.0f;
	else
		return (-b-sqrtf(discriminant))/(2.0f*a);
}

uint32_t planeSphereIntersect(const vec4 plane, const vec3 center, const float radius, vec3 *intersectionA, vec3 *intersectionB)
{
	const vec3 planeVec3=Vec3(plane.x, plane.y, plane.z);
	const float planeSphereSqDist=Vec3_Dot(planeVec3, center)+plane.w;
	const float planeSqLength=Vec3_Dot(planeVec3, planeVec3);

	const float distance=fabsf(planeSphereSqDist)/sqrtf(planeSqLength);

	if(distance>radius)
		return 0;

	const float projectionFactor=-planeSphereSqDist/planeSqLength;

	const vec3 projection=Vec3_Addv(center, Vec3_Muls(planeVec3, projectionFactor));

	const float distanceToIntersection=sqrtf(radius*radius-distance*distance);

	if(intersectionA)
		*intersectionA=Vec3_Addv(projection, Vec3_Muls(planeVec3, distanceToIntersection));

	if(intersectionB)
		*intersectionB=Vec3_Subv(projection, Vec3_Muls(planeVec3, distanceToIntersection));

	return (distance==radius)?1:2;
}
