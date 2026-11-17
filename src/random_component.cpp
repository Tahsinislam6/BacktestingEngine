#include "random_component.hpp"

#include <random>
#include <stdexcept>

namespace
{
    // splitmix64: a good way to turn (seed, counter) into an independent stream seed
    std::uint64_t mix(std::uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    // The statistics over `k` states, built from counts.
    class Counter
    {
    public:
        explicit Counter(std::size_t k) : k_(k), freq_(k, 0), trans_(k, std::vector<double>(k, 0.0)) {}

        void see(std::size_t state, bool hasPrev, std::size_t prev)
        {
            ++n_;
            freq_[state] += 1;
            if (hasPrev)
                trans_[prev][state] += 1;
        }

        ChainStats finish() const
        {
            ChainStats s;
            s.observations = n_;
            s.frequency.assign(k_, 1.0 / k_);
            if (n_ > 0)
            {
                for (std::size_t i = 0; i < k_; ++i)
                    s.frequency[i] = freq_[i] / n_;
            }
            s.transition.assign(k_, s.frequency);               // the fallback for a state never seen leaving
            for (std::size_t i = 0; i < k_; ++i)
            {
                double leaving = 0;
                for (double c : trans_[i])
                    leaving += c;
                if (leaving > 0)
                {
                    for (std::size_t j = 0; j < k_; ++j)
                        s.transition[i][j] = trans_[i][j] / leaving;
                }
            }
            return s;
        }

    private:
        std::size_t k_;
        std::size_t n_{0};
        std::vector<double> freq_;
        std::vector<std::vector<double>> trans_;
    };

    // The chain itself: draws the next state from a row of probabilities.
    class Chain
    {
    public:
        Chain(ChainStats stats, std::size_t warmup, std::uint64_t seed)
            : stats_(std::move(stats)), warmup_(warmup), seed_(seed), rng_(mix(seed))
        {
            if (stats_.frequency.empty() || stats_.transition.size() != stats_.frequency.size())
                throw std::invalid_argument("the chain statistics are empty or inconsistent");
        }

        // a fresh chain for the next asset: same statistics, its own stream
        Chain spawn() const { return Chain(stats_, warmup_, mix(seed_ ^ mix(++spawned_))); }

        void update()
        {
            ++seen_;
            if (seen_ == warmup_)
                state_ = draw(stats_.frequency);
            else if (seen_ > warmup_)
                state_ = draw(stats_.transition[state_]);
        }

        bool ready() const { return seen_ >= warmup_; }
        std::size_t state() const { return state_; }
        std::size_t warmup() const { return warmup_; }

    private:
        std::size_t draw(const std::vector<double>& probabilities)
        {
            const double u = static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);
            double edge = 0;
            for (std::size_t i = 0; i < probabilities.size(); ++i)
            {
                edge += probabilities[i];
                if (u < edge)
                    return i;
            }
            return probabilities.size() - 1;
        }

        ChainStats stats_;
        std::size_t warmup_;
        std::uint64_t seed_;
        std::mt19937_64 rng_;
        std::size_t seen_{0};
        std::size_t state_{0};
        mutable std::uint64_t spawned_{0};
    };

    class RandomState : public AnyState
    {
    public:
        explicit RandomState(Chain chain) : chain_(std::move(chain)) {}
        void update(const Bar&) override { chain_.update(); }
        bool ready() const override { return chain_.ready(); }
        int state() const override
        {
            if (!chain_.ready())
                throw std::logic_error("the random state is not ready yet");
            return static_cast<int>(chain_.state()) - 1;
        }
        std::size_t warmupBars() const override { return chain_.warmup(); }
        std::unique_ptr<AnyState> clone() const override { return std::make_unique<RandomState>(chain_.spawn()); }
    private:
        Chain chain_;
    };

    class RandomGate : public AnyGate
    {
    public:
        explicit RandomGate(Chain chain) : chain_(std::move(chain)) {}
        void update(const Bar&) override { chain_.update(); }
        bool ready() const override { return chain_.ready(); }
        bool allows() const override
        {
            if (!chain_.ready())
                throw std::logic_error("the random gate is not ready yet");
            return chain_.state() == 1;
        }
        std::size_t warmupBars() const override { return chain_.warmup(); }
        std::unique_ptr<AnyGate> clone() const override { return std::make_unique<RandomGate>(chain_.spawn()); }
    private:
        Chain chain_;
    };

    // Feeds a fresh copy of the component each asset's bars and records its states.
    template <typename Any, typename Index>
    ChainStats measure(const Any& prototype, const AlignedData& data, std::size_t states, Index indexOf)
    {
        Counter counter(states);
        for (std::size_t a = 0; a < data.symbols.size(); ++a)
        {
            std::unique_ptr<Any> copy = prototype.clone();
            bool hasPrev = false;
            std::size_t prev = 0;
            for (const Snapshot& snap : data.snapshots)
            {
                if (a >= snap.bars.size() || !snap.bars[a])
                    continue;
                copy->update(*snap.bars[a]);
                if (!copy->ready())
                    continue;
                const std::size_t now = indexOf(*copy);
                counter.see(now, hasPrev, prev);
                hasPrev = true;
                prev = now;
            }
        }
        return counter.finish();
    }
}

ChainStats measureState(const AnyState& prototype, const AlignedData& data)
{
    return measure(prototype, data, 3, [](const AnyState& s) { return static_cast<std::size_t>(s.state() + 1); });
}

ChainStats measureGate(const AnyGate& prototype, const AlignedData& data)
{
    return measure(prototype, data, 2, [](const AnyGate& g) { return static_cast<std::size_t>(g.allows() ? 1 : 0); });
}

std::shared_ptr<AnyState> randomState(const ChainStats& stats, std::size_t warmupBars, std::uint64_t seed)
{
    return std::make_shared<RandomState>(Chain(stats, warmupBars, seed));
}

std::shared_ptr<AnyGate> randomGate(const ChainStats& stats, std::size_t warmupBars, std::uint64_t seed)
{
    return std::make_shared<RandomGate>(Chain(stats, warmupBars, seed));
}
