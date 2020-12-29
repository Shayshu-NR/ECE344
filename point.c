#include <assert.h>
#include <math.h>
#include "common.h"
#include "point.h"

void
point_translate(struct point *p, double x, double y)
{
	//Just set p->x = p->x + x and the same for y wuing
	//the point_set function
	point_set(p, point_X(p) + x, point_Y(p) + y);
	return;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	//distance = sqrt( (x2-x1)^2 + (y2-y1)^2 )
	double y_distance = pow((point_Y(p2) - point_Y(p1)), 2.0);
	double x_distance = pow((point_X(p2) - point_X(p1)), 2.0);
	double cartesian_dist = sqrt(x_distance + y_distance);
	
	return cartesian_dist;
}

int
point_compare(const struct point *p1, const struct point *p2)
{	
	//Create origin point
	struct point origin;
	point_set(&origin, 0, 0);

	//Compare p1 to the origin
	//And compare p2 to the origin
	double p1_euclid = point_distance(&origin, p1);
	double p2_euclid = point_distance(&origin, p2);

	//If block to figure out which case to return
	if(p1_euclid > p2_euclid){
		return 1;
	}
	else if(p1_euclid < p2_euclid){
		return -1;
	}
	else{
		return 0;
	}
}
