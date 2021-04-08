#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>

class Timer
{
    private:
        using clock_t = std::chrono::high_resolution_clock;
        using second_t = std::chrono::duration<float, std::ratio<1> >;

        std::chrono::time_point<clock_t> m_start;
        float                            m_timeStamp;

    public:
        Timer()
            : m_start(clock_t::now())
        {
        }

        void reset()
        {
            m_start = clock_t::now();
        }

        void timeStamp()
        {
            m_timeStamp = (float)std::chrono::duration_cast<second_t>(clock_t::now() - m_start).count();
        }

        float getTime() const
        {
            return m_timeStamp;
        }
};

#endif // TIMER_HPP
