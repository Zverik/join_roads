#!/usr/bin/env python3
import sys
import osmium
import time
from collections import deque


class LineString:
    """Holds a list of sequential ways as tuples: (way_id, need_reverse)."""
    def __init__(self, way):
        reverse = way.tags.get('oneway', None) == '-1'
        self.ways = deque([(way.id, reverse)])
        self.ends = [way.nodes[0].ref, way.nodes[len(way.nodes)-1].ref]

    @staticmethod
    def reverse_ways(ways):
        return [(w[0], not w[1]) for w in reversed(ways)]

    def add(self, other):
        if isinstance(other, LineString):
            ends = other.ends
            ways = list(other.ways)
        else:
            reverse = other.tags.get('oneway', None) == '-1'
            ends = [other.nodes[0].ref, other.nodes[len(other.nodes)-1].ref]
            ways = [(other.id, reverse)]
        if ends[0] == self.ends[0] or ends[1] == self.ends[1]:
            ends.reverse()
            ways = self.reverse_ways(ways)

        if ends[1] == self.ends[0]:
            self.ways.extendleft(self.reverse_ways(ways))
            self.ends[0] = ends[0]
        elif ends[0] == self.ends[1]:
            self.ways.extend(ways)
            self.ends[1] = ends[1]
        else:
            return False
        return True


class Segments:
    def __init__(self, way):
        self.parts = [LineString(way)]

    def add(self, way):
        found = -1
        for i, s in enumerate(self.parts):
            if s.add(way):
                found = i
                break
        if found < 0:
            self.parts.append(LineString(way))
        else:
            for part in self.parts:
                if part.add(self.parts[found]):
                    del self.parts[found]
                    break

    def get_long_ways(self):
        return [part.ways for part in self.parts if len(part.ways) > 1]


MAX_WAY_ID = 496751025


class WayParser(osmium.SimpleHandler):
    def __init__(self):
        super(WayParser, self).__init__()
        self.data = {}
        self.count = 0
        self.start_time = None

    def way(self, way):
        if self.start_time is None:
            self.start_time = time.time()
        self.count += 1
        if self.count % 100000 == 0:
            ways_per_sec = self.count / (time.time() - self.start_time)
            elapsed = (MAX_WAY_ID-way.id) * 0.84 / ways_per_sec  # there are 83.3% existing ways
            sys.stderr.write('\r{:.2f}% (elapsed {} minutes)   '.format(100.0*way.id / MAX_WAY_ID, int(elapsed/60)))

        if 'highway' not in way.tags:
            return
        if 'name' not in way.tags and 'ref' not in way.tags:
            return
        key = (way.tags.get('name', ''), way.tags.get('ref', ''))
        if key not in self.data:
            self.data[key] = Segments(way)
        else:
            self.data[key].add(way)

    def get_result(self):
        result = []
        for k, s in self.data.items():
            r = s.get_long_ways()
            for w in r:
                result.append([w, k[0], k[1]])
        return result


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Merges ways with similar name and ref tags and produces a csv file')
        print('Usage: {} <file.osm.pbf>'.format(sys.argv[0]))
        sys.exit(1)

w = WayParser()
w.apply_file(sys.argv[1])
sys.stderr.write('\r{:<30}\n'.format('100%'))
for r in w.get_result():
    print(','.join([str(-x[0] if x[1] else x[0]) for x in r[0]]))
