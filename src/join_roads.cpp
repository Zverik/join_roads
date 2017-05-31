#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <iomanip>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/o5m_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

std::uint32_t constexpr kMaxWayId = 500000000;

using TWays = std::deque<std::int32_t>;

class LineString
{
  TWays m_ways;
  std::uint64_t m_start;
  std::uint64_t m_end;

public:
  explicit LineString(osmium::Way const & way)
  {
    bool const reverse = way.tags().has_tag("oneway", "-1");
    m_ways.push_back(reverse ? -way.id() : way.id());
    m_start = way.nodes().front().ref();
    m_end = way.nodes().back().ref();
  }

  TWays const & Ways() const { return m_ways; }

  void reverse()
  {
    auto const tmp = m_end;
    m_end = m_start;
    m_start = tmp;
    std::reverse(m_ways.begin(), m_ways.end());
    for (auto & p : m_ways)
      p = -p;
  }

  bool add(LineString & line)
  {
    if (m_start == line.m_start || m_end == line.m_end)
      line.reverse();
    if (m_end == line.m_start)
    {
      m_ways.insert(m_ways.end(), line.m_ways.begin(), line.m_ways.end());
      m_end = line.m_end;
    }
    else if (m_start == line.m_end)
    {
      m_ways.insert(m_ways.begin(), line.m_ways.begin(), line.m_ways.end());
      m_start = line.m_start;
    }
    else
      return false;
    return true;
  }
};

class Segments
{
  std::list<LineString> m_parts;

public:
  explicit Segments(osmium::Way const & way)
  {
    m_parts.push_back(LineString(way));
  }

  void add(osmium::Way const & way)
  {
    LineString line(way);
    auto found = m_parts.end();
    for (auto i = m_parts.begin(); i != m_parts.end(); ++i)
    {
      if (i->add(line))
      {
        found = i;
        break;
      }
    }
    if (found == m_parts.cend())
    {
      m_parts.push_back(line);
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

class RoadHandler : public osmium::handler::Handler
{
  std::unordered_map<std::size_t, Segments> m_data;
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
        for (auto const & way : ways)
        {
          if (way != ways.front())
            std::cout << ',';
          std::cout << way;
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
    if (!(m_count & 0xFFFFF))
    {
      double const ways_per_sec = double(m_count) / (std::time(nullptr) - m_startTime);
      std::uint32_t const elapsed = (kMaxWayId - way.id()) / ways_per_sec;
      std::cerr << '\r' << std::setprecision(3) << (100.0 * way.id() / kMaxWayId) << "% (est. " << (elapsed / 60) << " min)   ";
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

    std::size_t const key = std::hash<std::string>{}(name + '\0' + ref);
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
