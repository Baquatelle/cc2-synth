#include "ParticleField.h"

#include "../audio/SynthEngine.h"
#include "../audio/Voice.h"

#include "ofGraphics.h"

#include <algorithm>
#include <random>

namespace
{

std::mt19937& rng()
{
    static std::mt19937 generator{20260818u};
    return generator;
}

float randomRange(float aLow, float aHigh)
{
    std::uniform_real_distribution<float> distribution(aLow, aHigh);
    return distribution(rng());
}

ofColor colorFor(VoiceType aType)
{
    switch (aType)
    {
    case VoiceType::FM:
        return {120, 200, 255};
    case VoiceType::Percussion:
        return {255, 160, 90};
    case VoiceType::Sampler:
        return {180, 255, 150};
    }
    return {255, 255, 255};
}

} // namespace

ParticleField::ParticleField()
{
    // Allocated once, here. Nothing in spawn/update/draw allocates again, so the
    // per-frame cost stays flat no matter how hard the sequencer is driven.
    mParticles.resize(kMaxParticles);
}

void ParticleField::spawn(VoiceType aType, int aMidiNote, float aVelocity, float aX, float aY)
{
    // Each timbre gets its own gesture, so the picture identifies the sound:
    // percussion bursts outward hard and dies fast, FM drifts upward and lingers,
    // the sampler sits between the two.
    int   count = 14;
    float speed = 90.0f;
    float decay = 1.4f;

    switch (aType)
    {
    case VoiceType::Percussion:
        count = 22;
        speed = 190.0f;
        decay = 3.0f;
        break;
    case VoiceType::FM:
        count = 16;
        speed = 70.0f;
        decay = 1.1f;
        break;
    case VoiceType::Sampler:
        count = 18;
        speed = 120.0f;
        decay = 1.7f;
        break;
    }

    count = static_cast<int>(static_cast<float>(count) * (0.5f + 0.5f * aVelocity));

    for (int i = 0; i < count; ++i)
    {
        // Round-robin over the fixed pool: when it is full the oldest particle is
        // overwritten, which is imperceptible and keeps the cost bounded.
        Particle& particle = mParticles[mNextParticle];
        mNextParticle      = (mNextParticle + 1) % kMaxParticles;

        const float angle     = randomRange(0.0f, dsp::kTwoPi);
        const float magnitude = randomRange(0.25f, 1.0f) * speed * (0.6f + 0.4f * aVelocity);

        particle.mX         = aX + randomRange(-4.0f, 4.0f);
        particle.mY         = aY + randomRange(-4.0f, 4.0f);
        particle.mVelocityX = std::cos(angle) * magnitude;
        particle.mVelocityY = std::sin(angle) * magnitude;
        particle.mLife      = 1.0f;
        particle.mDecay     = decay * randomRange(0.8f, 1.3f);
        particle.mSize      = randomRange(1.5f, 4.0f) * (0.7f + 0.6f * aVelocity);
        particle.mType      = aType;
    }

    (void)aMidiNote;
}

void ParticleField::update(float aDeltaSeconds)
{
    if (aDeltaSeconds <= 0.0f)
    {
        return;
    }

    // Read real synthesis state: the summed envelope level of every sounding voice.
    float targetEnergy = 0.0f;
    if (mEngine != nullptr)
    {
        for (const auto& voice : mEngine->voices())
        {
            if (voice->isActive())
            {
                targetEnergy += voice->envelopeLevel();
            }
        }
    }

    // Smooth so the glow breathes rather than flickering at frame rate.
    mEnergy += (targetEnergy - mEnergy) * std::min(1.0f, aDeltaSeconds * 8.0f);

    for (Particle& particle : mParticles)
    {
        if (particle.mLife <= 0.0f)
        {
            continue; // unused slot
        }

        particle.mX += particle.mVelocityX * aDeltaSeconds;
        particle.mY += particle.mVelocityY * aDeltaSeconds;

        // Mild drag plus a slight upward drift, which reads as "rising sparks".
        particle.mVelocityX *= (1.0f - 2.0f * aDeltaSeconds);
        particle.mVelocityY = particle.mVelocityY * (1.0f - 2.0f * aDeltaSeconds) - 18.0f * aDeltaSeconds;

        particle.mLife -= particle.mDecay * aDeltaSeconds;
        if (particle.mLife < 0.0f)
        {
            particle.mLife = 0.0f; // returns the slot to the pool
        }
    }
}

void ParticleField::draw() const
{
    // Additive blending makes overlapping particles bloom, which is what sells the
    // effect when several voices sound at once.
    ofEnableBlendMode(OF_BLENDMODE_ADD);

    // Overall brightness follows how much sound is actually being produced.
    const float energyScale = std::min(1.0f, 0.35f + mEnergy * 0.30f);

    for (const Particle& particle : mParticles)
    {
        if (particle.mLife <= 0.0f)
        {
            continue;
        }

        const float life  = particle.mLife;
        ofColor     color = colorFor(particle.mType);
        color.a           = static_cast<unsigned char>(std::min(255.0f, 230.0f * life * energyScale));

        ofSetColor(color);
        ofDrawCircle(particle.mX, particle.mY, particle.mSize * (0.4f + 0.6f * life));
    }

    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(255);
}

std::size_t ParticleField::particleCount() const
{
    std::size_t alive = 0;
    for (const Particle& particle : mParticles)
    {
        if (particle.mLife > 0.0f)
        {
            ++alive;
        }
    }
    return alive;
}
