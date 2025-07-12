#ifndef __PARTICLE_H__
#define __PARTICLE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <threads.h>
#include "../utils/list.h"
#include "../math/math.h"
#include "../vulkan/vulkan.h"

typedef struct Particle_s
{
	vec3 position, velocity;
	vec3 startColor, endColor;
	float particleSize;
	float life;
} Particle_t;

typedef void (*ParticleInitCallback)(uint32_t index, uint32_t numParticles, Particle_t *particle);

typedef enum ParticleEmitterType_e
{
	PARTICLE_EMITTER_CONTINOUS=0,
	PARTICLE_EMITTER_BURST=1,
	PARTICLE_EMITTER_ONCE=2,
} ParticleEmitterType_e;

typedef struct ParticleEmitter_s
{
	uint32_t ID;
	ParticleEmitterType_e type;
	vec3 position;
	vec3 startColor, endColor;
	float particleSize;
	float emissionRate, emissionInterval, emissionTime;

	ParticleInitCallback initCallback;
} ParticleEmitter_t;

typedef struct ParticleSystem_s
{
	uint32_t baseID;

	vec3 gravity;

	List_t emitters;

	uint32_t numParticles, maxParticles;
	Particle_t *particles;

	mtx_t mutex;

	VkuBuffer_t particleBuffer[VKU_MAX_FRAME_COUNT];
	vec4 *systemBuffer;
} ParticleSystem_t;

uint32_t ParticleSystem_AddEmitter(ParticleSystem_t *system, vec3 position, vec3 startColor, vec3 endColor, float particleSize, uint32_t numParticles, ParticleEmitterType_e type, ParticleInitCallback initCallback);
void ParticleSystem_DeleteEmitter(ParticleSystem_t *system, uint32_t ID);
void ParticleSystem_ResetEmitter(ParticleSystem_t *system, uint32_t ID);
void ParticleSystem_SetEmitterPosition(ParticleSystem_t *system, uint32_t ID, vec3 position);

bool ParticleSystem_SetGravity(ParticleSystem_t *system, float x, float y, float z);
bool ParticleSystem_SetGravityv(ParticleSystem_t *system, vec3 v);

bool ParticleSystem_Init(ParticleSystem_t *system);
void ParticleSystem_Step(ParticleSystem_t *system, float dt);
void ParticleSystem_Draw(ParticleSystem_t *system, VkCommandBuffer commandBuffer, uint32_t index, uint32_t eye);
void ParticleSystem_Destroy(ParticleSystem_t *system);

#endif
