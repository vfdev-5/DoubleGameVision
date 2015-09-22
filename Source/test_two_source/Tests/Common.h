#ifndef COMMON_H
#define COMMON_H

// Opencv
#include <opencv2/core/core.hpp>

namespace Tests {

//*************************************************************************

cv::Mat generateSimpleGeometries();
cv::Mat generateEllipseLikeGeometries();

cv::Mat generateBigObjects();

//*************************************************************************

}


#endif // COMMON_H
