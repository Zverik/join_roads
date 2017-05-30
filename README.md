# Join Roads

This is a quick C++ script for processing the OSM planet file and printing
a CSV file of way identifiers.

## Usage

```bash
mkdir build
cd build
cmake ..
make
./join_roads ~/osm/planet-latest.osm.pbf > planet-roads.csv
```

## Author and License

Written by Ilya Zverev, published under WTFPL license.
