#ifndef SCANPATH_H
#define SCANPATH_H

#include <vector>

struct Point2D {
    float x;
    float y;
};

// 弓形扫描路径生成
//   scanLength：扫查轴长(可正可负，符号决定方向)
//   stepLength：步进轴长(可正可负，符号决定方向；==0 表示单轴往复)
//   stepSize  ：步进长度(==0 或 stepLength==0 时退化为扫查轴单轴往复)
// 点坐标为 (扫查轴, 步进轴)，相邻两点之间只有一个轴在动，方便分轴下发。
std::vector<Point2D> generateBowScanPath(float scanLength, float stepLength, float stepSize);

#endif // SCANPATH_H
