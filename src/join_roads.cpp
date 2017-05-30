#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <algorithm>
#include <iostream>
#include <iomanip>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/o5m_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

using TWayId = std::uint32_t;
using TNodeId = std::uint64_t;
using TWays = std::deque<std::pair<TWayId, bool>>;

class LineString
{
  TWays m_ways;
  std::pair<TNodeId, TNodeId> m_ends;

public:
  explicit LineString(osmium::Way const & way)
  {
    bool reverse = way.tags().has_tag("oneway", "-1");
    m_ways.push_back({way.id(), reverse});
    m_ends = {way.nodes().front().ref(), way.nodes().back().ref()};
  }

  TWays const & Ways() const { return m_ways; }

  void reverse()
  {
    m_ends = {m_ends.second, m_ends.first};
    std::reverse(m_ways.begin(), m_ways.end());
    for (auto & p : m_ways)
      p.second = !p.second;
  }

  bool add(osmium::Way const & way)
  {
    LineString line(way);
    return add(line);
  }

  bool add(LineString & line)
  {
    if (m_ends.first == line.m_ends.first || m_ends.second == line.m_ends.second)
      line.reverse();
    if (m_ends.second == line.m_ends.first)
    {
      m_ways.insert(m_ways.end(), line.m_ways.begin(), line.m_ways.end());
      m_ends.second = line.m_ends.second;
    }
    else if (m_ends.first == line.m_ends.second)
    {
      m_ways.insert(m_ways.begin(), line.m_ways.begin(), line.m_ways.end());
      m_ends.first = line.m_ends.first;
    }
    else
      return false;
    return true;
  }
};

class Segments
{
  std::vector<LineString> m_parts;

public:
  explicit Segments(osmium::Way const & way)
  {
    m_parts.push_back(LineString(way));
  }

  void add(osmium::Way const & way)
  {
    auto found = m_parts.end();
    for (auto i = m_parts.begin(); i != m_parts.end(); ++i)
    {
      if (i->add(way))
      {
        found = i;
        break;
      }
    }
    if (found == m_parts.cend())
    {
      m_parts.push_back(LineString(way));
    }
    else
    {
      for (LineString & part : m_parts)
      {
        if (part.add(*found))
        {
          m_parts.erase(found);
          break;
        }
      }
    }
  }

  std::vector<TWays> getLongWays() const
  {
    std::vector<TWays> result;
    for (LineString const & line : m_parts)
      if (line.Ways().size() > 1)
        result.push_back(line.Ways());
    return result;
  }
};

std::uint32_t constexpr kMaxWayId = 497000000;

class RoadHandler : public osmium::handler::Handler
{
  using TSegmentKey = std::pair<std::string, std::string>;
  std::map<TSegmentKey, Segments> m_data;
  std::uint32_t m_count;
  std::time_t m_startTime;

public:
  RoadHandler(): m_count(0) {};

  void print_result()
  {
    for (auto const & seg : m_data)
    {
      auto const & longWays = seg.second.getLongWays();
      for (TWays const & ways : longWays)
      {
        if (ways.size() <= 1)
          continue;
        bool first = true;
        for (auto const & wayRev : ways)
        {
          if (first)
            first = false;
          else
            std::cout << ',';
          if (wayRev.second)
            std::cout << '-';
          std::cout << wayRev.first;
        }
        std::cout << std::endl;
      }
    }
  }

  void way(osmium::Way const & way)
  {
    if (!m_count)
      m_startTime = std::time(nullptr);
    m_count++;
    if (m_count % 100000 == 0)
    {
      double ways_per_sec = double(m_count) / (std::time(nullptr) - m_startTime);
      std::uint32_t elapsed = (kMaxWayId - way.id()) / ways_per_sec;
      std::cerr << '\r' << std::setprecision(4) << (100.0 * way.id() / kMaxWayId) << "% (est. " << (elapsed / 60) << " min)   ";
    }

    bool foundHighway = false;
    std::string name;
    std::string ref;
    for (auto const & tag : way.tags())
    {
      if (!foundHighway && !strcmp(tag.key(), "highway"))
        foundHighway = true;
      else if (!strcmp(tag.key(), "name"))
        name = tag.value();
      else if (!strcmp(tag.key(), "ref"))
        ref = tag.value();
    }
    if (!foundHighway || (name.empty() && ref.empty()))
      return;

    TSegmentKey key{name, ref};
    auto segment = m_data.find(key);
    if (segment == m_data.cend())
      m_data.emplace(key, Segments(way));
    else
      segment->second.add(way);
  }

  void flush() const
  {
    std::cerr << '\r' << std::setw(20) << std::left << "100%" << std::endl;
  }
};

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " OSMFILE" << std::endl;
    std::exit(1);
  }
  RoadHandler roadHandler;
  osmium::io::Reader reader{argv[1], osmium::osm_entity_bits::way};
  osmium::apply(reader, roadHandler);
  roadHandler.print_result();
}
