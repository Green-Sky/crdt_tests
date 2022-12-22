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
	- 23.0s
	- 22.6s
	- 23.0s

- g++9 -O3 -DNDEBUG :
	- 9.6s
	- 9.7s
	- 9.7s


## version0

- g++9 -g -O2 :
	- 10m35s

- g++9 -O3 -DNDEBUG :
	- 8m7s

## version1

