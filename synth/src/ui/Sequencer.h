#pragma once

#include "../audio/Note.h"

#include <array>
#include <cstddef>
#include <vector>

class SynthEngine;

/// Clickable pitch x step grid that plays the synthesiser.
///
/// ------------------------------------------------------------------------
/// ASSOCIATION (the "uses, but does not own" relationship)
/// ------------------------------------------------------------------------
/// The sequencer holds a non-owning `SynthEngine *`. It *drives* the engine by
/// calling `noteOn`/`noteOff`, but:
///   * it did not create the engine and will not destroy it,
///   * the engine has no idea the sequencer exists,
///   * and either object can be destroyed without invalidating the other's state.
class Sequencer
{
  public:
    static constexpr std::size_t kSteps = 16;
    static constexpr std::size_t kRows  = 8;

    /// Distinguishes sequencer notes from keyboard notes of the same pitch.
    static constexpr std::uint8_t kSourceId = 1;

    /// One grid row: a fixed voice type and pitch, toggled per step.
    struct Row
    {
        VoiceType                mVoiceType = VoiceType::FM;
        int                      mMidiNote  = 60;
        std::array<bool, kSteps> mSteps{};
    };

    /// Attaches the engine this sequencer drives. Non-owning.
    void attach(SynthEngine* aEngine)
    {
        mEngine = aEngine;
    }

    /// Builds the default rows (drums on the low rows, melodic above).
    void setupDefaultPattern();

    /// Advances the playhead by `aDeltaSeconds`, triggering steps and releasing
    /// notes whose gate has expired.
    void advance(float aDeltaSeconds);

    void togglePlaying()
    {
        mPlaying = !mPlaying;
    }

    bool isPlaying() const
    {
        return mPlaying;
    }

    void setPlaying(bool aPlaying)
    {
        mPlaying = aPlaying;
    }

    void  setTempo(float aBpm);
    float tempo() const
    {
        return mBpm;
    }

    void toggleStep(std::size_t aRow, std::size_t aStep);
    bool stepEnabled(std::size_t aRow, std::size_t aStep) const;

    void clear();

    /// Fills the grid with a musically plausible random pattern.
    void randomize(unsigned int aSeed);

    std::size_t currentStep() const
    {
        return mCurrentStep;
    }

    const std::array<Row, kRows>& rows() const
    {
        return mRows;
    }

    /// Total notes triggered since construction; the self-test asserts on this.
    std::size_t triggeredNoteCount() const
    {
        return mTriggeredNotes;
    }

    // --- Drawing (UI thread only; requires a GL context) ---------------------

    /// Draws the grid. `aX`, `aY`, `aWidth`, `aHeight` bound the whole matrix.
    void draw(float aX, float aY, float aWidth, float aHeight) const;

    /// Translates a click into a step toggle. Returns true if it hit the grid.
    bool handleClick(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight);

    /// Continues a paint gesture started by `handleClick`. Each newly entered cell
    /// is toggled exactly once, so dragging across the grid draws a run of steps
    /// instead of flickering the same cell on and off.
    bool handleDrag(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight);

    /// Ends a paint gesture (mouse release).
    void endDrag();

  private:
    void  triggerStep(std::size_t aStep);
    void  releaseExpiredNotes(float aDeltaSeconds);
    float secondsPerStep() const;

    /// Plays a single row immediately, for click feedback.
    void auditionRow(std::size_t aRow);

    /// Maps a pointer position to a cell. Returns false if outside the grid.
    bool cellAt(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight, std::size_t& aOutRow,
                std::size_t& aOutStep) const;

    /// A note the sequencer started and still owes a note-off.
    struct HeldNote
    {
        VoiceType mVoiceType        = VoiceType::FM;
        int       mMidiNote         = 60;
        float     mRemainingSeconds = 0.0f;
    };

    /// Association: used, never owned.
    SynthEngine* mEngine = nullptr;

    std::array<Row, kRows> mRows{};
    std::vector<HeldNote>  mHeldNotes;

    bool        mPlaying        = false;
    float       mBpm            = 110.0f;
    float       mStepTimer      = 0.0f;
    std::size_t mCurrentStep    = 0;
    std::size_t mTriggeredNotes = 0;

    /// Fraction of a step that a triggered note is held for.
    float mGateRatio = 0.55f;

    // Paint-gesture state, so a drag toggles each cell at most once.
    bool        mDragging        = false;
    std::size_t mLastPaintedRow  = kRows;
    std::size_t mLastPaintedStep = kSteps;
};
