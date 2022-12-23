# Timings

all benches use the uncompressed .json from disk

all done with the "cool an breezy copy" preset

note: the .json is not a perfect fit, bc it is not designed for the Yjs algo (missing parent_right and ids are not perfect)

the json contains:
	doc size (with tombstones): 182315
	doc size: 104852
	total inserts: 182315
	total deletes: 77463
	total ops: 259778

## baseline ( just walking through the json, no insertions )

- g++9 -g :
	- 23.0s		~11294 ops/s
	- 22.6s		~11494 ops/s
	- 23.0s

- g++9 -O3 -DNDEBUG :
	- 9.6s		~27060 ops/s
	- 9.7s
	- 9.7s


## version0

- g++9 -g -O2 :
	- 10m35s	~409 ops/s

- g++9 -O3 -DNDEBUG :
	- 8m7s		~533 ops/s

## version1 - actor index

- g++9 -g -O2 :
	- 4m1s		~1077 ops/s

- g++9 -O3 -DNDEBUG :
	- 4m5s		~1060 ops/s

## version2 - find with hint, cache last insert and use as hint

- g++9 -g -O2 :
	- 3m38s		~1191 ops/s

- g++9 -O3 -DNDEBUG :
	- 3m43s		~1164 ops/s

## version3 - SoA, 1 array only ids, 1 array rest (parents, data)

- g++9 -g :
	- 4m19s		~1003 ops/s

- g++9 -g -O2 :
	- 3m36s		~1202 ops/s

- g++9 -O3 -DNDEBUG :
	- 3m44s		~1159 ops/s

