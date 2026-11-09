#pragma once

#include <QObject>

namespace komapaint {

// State backing the bottom timeline panel. In Phase 1 this holds only the
// dimensions and playback position; actual frame data, keyframes and
// playback will arrive in later phases.

class TimelineState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int frameCount    READ frameCount    WRITE setFrameCount    NOTIFY frameCountChanged)
    Q_PROPERTY(int currentFrame  READ currentFrame  WRITE setCurrentFrame  NOTIFY currentFrameChanged)
    Q_PROPERTY(int fps           READ fps           WRITE setFps           NOTIFY fpsChanged)
    Q_PROPERTY(bool playing      READ isPlaying     WRITE setPlaying       NOTIFY playingChanged)

public:
    explicit TimelineState(QObject* parent = nullptr);

    int  frameCount()   const { return m_frameCount; }
    int  currentFrame() const { return m_currentFrame; }
    int  fps()          const { return m_fps; }
    bool isPlaying()    const { return m_playing; }

public slots:
    void setFrameCount(int n);
    void setCurrentFrame(int f);
    void setFps(int fps);
    void setPlaying(bool p);
    void play()  { setPlaying(true);  }
    void pause() { setPlaying(false); }
    void stop();

signals:
    void frameCountChanged();
    void currentFrameChanged();
    void fpsChanged();
    void playingChanged();

private:
    int  m_frameCount   = 24;
    int  m_currentFrame = 0;
    int  m_fps          = 24;
    bool m_playing      = false;
};

}  // namespace komapaint
