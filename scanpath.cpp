#include "scanpath.h"
#include "locke_log.h"

#include <QtGlobal>
#include <cmath>

std::vector<Point2D> generateBowScanPath(float scanLength, float stepLength, float stepSize)
{
    std::vector<Point2D> path;
    path.push_back({0.0f, 0.0f});

    // 退化情形：仅扫查轴往复一次 (0,0) → (scanLength,0)，勾选"循环执行"时就是不停往复
    if (qFuzzyIsNull(stepLength) || qFuzzyIsNull(stepSize)) {
        if (!qFuzzyIsNull(scanLength)) {
            path.push_back({scanLength, 0.0f});
        }
        qCInfo(lcScan) << "生成路径点数:" << int(path.size()) << "(single-axis sweep)";
        return path;
    }

    const float sign = (stepLength >= 0) ? 1.0f : -1.0f;
    const float delta = sign * std::fabs(stepSize);
    const int maxPoints = 10000;

    auto notReachedEnd = [&](float value) {
        return sign > 0 ? (value <= stepLength + 1e-4f) : (value >= stepLength - 1e-4f);
    };

    bool forward = true;
    float scan = 0.0f;
    float step = 0.0f;
    while (notReachedEnd(step) && int(path.size()) < maxPoints) {
        scan = forward ? scanLength : 0.0f;
        path.push_back({scan, step});

        step += delta;
        if (!notReachedEnd(step)) break;

        path.push_back({scan, step});
        forward = !forward;
    }

    if (int(path.size()) >= maxPoints)
        qCWarning(lcScan) << "路径点数达到上限" << maxPoints << "，已截断，检查步进长度是否过小";
    qCInfo(lcScan) << "生成路径点数:" << int(path.size())
                   << "xLen:" << scanLength << "yLen:" << stepLength << "yStep:" << stepSize;
    return path;
}
