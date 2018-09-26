#ifndef OPTIMIZER_H__
#define OPTIMIZER_H__

#include "Mapping.h"

class Optimizer {

public:

	const int NUM_LOCAL_KF = 7;

	Optimizer();

	void run();

	void LocalBA();

	void GlobalBA();

	void SetMap(Mapping * map_);

protected:

	Mapping * map;

	size_t noKeyFrames;
};

#endif
