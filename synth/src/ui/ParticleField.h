#pragma once

#include "../audio/Note.h"

#include <cstddef>
#include <vector>

class SynthEngine;

/// Note-reactive particle bloom.
///
/// ASSOCIATION: keeps a `const SynthEngine *` to read live voice state. It owns its
/// own particles (composition of a different kind -- a plain vector of values), but
/// it neither owns nor mutates the engine.
///
/// Particles are spawned explicitly by the app when a note is triggered, and their
/// brightness is then scaled by the *actual* envelope level of the sounding voices.
/// That is the visible payoff of the polymorphic pool: the visualiser reads real
/// synthesis state rather than re-deriving an approximation of it.
///
/// The particle store is a fixed pool allocated once, overwritten round-robin.
/// A growing vector with an erase-remove pass every frame would make the per-frame
/// cost depend on how hard the sequencer is running, which is exactly the kind of
/// unbounded work a real-time visualiser should avoid.
class ParticleField
{
  public:
    /// Plenty for a dense pattern; overwriting the oldest particle is imperceptible.
    static constexpr std::size_t kMaxParticles = 1024;

    ParticleField();

    void attach(const SynthEngine* aEngine)
    {
        mEngine = aEngine;
    }

    /// Emits a burst for a note. `aMidiNote` positions the burst horizontally so
    /// pitch maps to screen position.
    void spawn(VoiceType aType, int aMidiNote, float aVelocity, float aX, float aY);

    void update(float aDeltaSeconds);
    void draw() const;

    /// Number of particles currently alive (the self-test asserts on this).
    std::size_t particleCount() const;

  private:
    struct Particle
    {
        float     mX         = 0.0f;
        float     mY         = 0.0f;
        float     mVelocityX = 0.0f;
        float     mVelocityY = 0.0f;
        float     mLife      = 0.0f; // 1 -> 0; 0 means unused
        float     mDecay     = 1.0f; // life units per second
        float     mSize      = 3.0f;
        VoiceType mType      = VoiceType::FM;
    };

    /// Association: observed, never owned.
    const SynthEngine* mEngine = nullptr;

    /// Fixed-size pool, allocated once in the constructor.
    std::vector<Particle> mParticles;
    std::size_t           mNextParticle = 0;

    /// Smoothed sum of active envelope levels, used to scale overall brightness.
    float mEnergy = 0.0f;
};
