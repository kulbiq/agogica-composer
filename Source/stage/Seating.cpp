#include "Seating.h"
#include "Places.h"

namespace stage
{
geometry::Point freeSeat (geometry::Point listener, float roomSize, const std::vector<geometry::Point>& taken)
{
    const auto spacing = std::clamp (roomSize / 10.0f, 0.75f, 2.0f);
    const auto edge = roomSize * 0.5f - 0.5f; // seats keep 0.5 m from the walls
    const auto inside = [&] (geometry::Point p) { return std::abs (p.x) <= edge + 1.0e-4f && std::abs (p.y) <= edge + 1.0e-4f; };
    const auto nearest = [&] (geometry::Point p)
    {
        auto d = std::numeric_limits<float>::max();
        for (const auto& t : taken)
            d = std::min (d, geometry::distance (p, t));
        return d;
    };

    // The first row: 3 spacings in front of the listener, or as far as the front wall allows.
    const geometry::Point front { listener.x, std::min (listener.y + 3.0f * spacing, edge) };
    const auto reach = (int) std::ceil (roomSize / spacing) + 1;

    // Seats on a grid through the first row, in order of preference (lower = better): rows of 7
    // front first, then wider rows, then rows nearer the listener, then beside and behind;
    // within a row, centre-out (0, -1, 1, -2, 2 ...).
    struct Seat { std::array<int, 3> rank; geometry::Point at; };
    std::vector<Seat> seats;

    for (int row = -2 * reach; row <= reach; ++row)
        for (int column = -reach; column <= reach; ++column)
        {
            const geometry::Point at { front.x + spacing * (float) column, front.y + spacing * (float) row };
            if (! inside (at) || geometry::distance (at, listener) < spacing)
                continue;

            const auto tier = row >= 0 ? (std::abs (column) <= 3 ? 0 : 1)
                                       : (at.y > listener.y + 0.5f * spacing ? 2 : 3);
            seats.push_back ({ { tier, std::abs (row), 2 * std::abs (column) - (column < 0 ? 1 : 0) }, at });
        }

    std::sort (seats.begin(), seats.end(), [] (const Seat& a, const Seat& b) { return a.rank < b.rank; });

    for (const auto& seat : seats)
        if (nearest (seat.at) >= 0.5f * spacing)
            return seat.at;

    // Full: the point (on a finer grid) farthest from everyone, never on top of another.
    auto best = front;
    auto bestDistance = -1.0f;
    const auto step = 0.25f * spacing;
    for (auto y = -edge; y <= edge + 1.0e-4f; y += step)
        for (auto x = -edge; x <= edge + 1.0e-4f; x += step)
            if (const auto d = nearest ({ x, y }); d > bestDistance)
                best = { x, y }, bestDistance = d;

    return best;
}


void join (ComposerProcessor& self)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        ComposerProcessor* source = nullptr;

        for (auto* p : members)
            if (source == nullptr && p != &self && p->isPlaced())
                source = p; // the first one placed

        if (source != nullptr)
        {
            self.setActiveScene (source->getScene());

            for (const auto& paramId : params::ofStage)
                self.writeParam (paramId, source->getParam (paramId));

            self.setHeadTrackingOn (source->isHeadTrackingOn());
            self.setAudience (source->getAudience());

            // Where everyone is drawn (clamped to the room), not their stored values.
            const auto roomSize = self.getParam (params::roomSize);
            std::vector<geometry::Point> taken;
            for (auto* p : members)
                if (p != &self)
                    taken.push_back (playerPlace (*p, roomSize));

            const auto s = freeSeat (listenerPlace (self), roomSize, taken);
            self.writeParam (params::playerX, s.x);
            self.writeParam (params::playerY, s.y);
        }

        self.markPlaced();
    });
}
} // namespace stage
