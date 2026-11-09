#include "TimelineState.h"

namespace komapaint {

TimelineState::TimelineState(QObject* parent) : QObject(parent) {}

void TimelineState::setFrameCount(int n)
{
    n = qMax(1, n);
    if (n == m_frameCount) return;
    m_frameCount = n;
    if (m_currentFrame >= m_frameCount) setCurrentFrame(m_frameCount - 1);
    emit frameCountChanged();
}

void TimelineState::setCurrentFrame(int f)
{
    f = qBound(0, f, m_frameCount - 1);
    if (f == m_currentFrame) return;
    m_currentFrame = f;
    emit currentFrameChanged();
}

void TimelineState::setFps(int fps)
{
    fps = qBound(1, fps, 120);
    if (fps == m_fps) return;
    m_fps = fps;
    emit fpsChanged();
}

void TimelineState::setPlaying(bool p)
{
    if (p == m_playing) return;
    m_playing = p;
    emit playingChanged();
}

void TimelineState::stop()
{
    setPlaying(false);
    setCurrentFrame(0);
}

}  // namespace komapaint
