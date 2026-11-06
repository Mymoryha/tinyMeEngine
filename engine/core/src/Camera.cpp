#include <tinyme/core/Camera.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace tinyme
{
namespace
{
using Vector = std::array<double, 3>;
double Dot(const Vector& a, const Vector& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector Cross(const Vector& a, const Vector& b) noexcept
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vector Normalize(Vector value) noexcept
{
    const double length = std::sqrt(Dot(value, value));
    for (double& component : value)
    {
        component /= length;
    }
    return value;
}
Vector Direction(const CameraParameters& camera) noexcept
{
    return {static_cast<double>(camera.target[0]) - camera.eye[0],
            static_cast<double>(camera.target[1]) - camera.eye[1],
            static_cast<double>(camera.target[2]) - camera.eye[2]};
}
Vector Up(const CameraParameters& camera) noexcept
{
    return {camera.up[0], camera.up[1], camera.up[2]};
}
} // namespace

bool IsValid(const CameraParameters& camera) noexcept
{
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (!std::isfinite(camera.eye[index]) || !std::isfinite(camera.target[index]) ||
            !std::isfinite(camera.up[index]) || std::abs(camera.eye[index]) > 10000.0F ||
            std::abs(camera.target[index]) > 10000.0F)
        {
            return false;
        }
    }
    if (!std::isfinite(camera.vertical_fov_degrees) || camera.vertical_fov_degrees < 10.0F ||
        camera.vertical_fov_degrees > 120.0F || !std::isfinite(camera.near_plane) ||
        camera.near_plane < 0.001F || camera.near_plane > 1000.0F ||
        !std::isfinite(camera.far_plane) || camera.far_plane > 100000.0F ||
        static_cast<double>(camera.far_plane) - camera.near_plane < 0.001)
    {
        return false;
    }
    const auto direction = Direction(camera);
    const auto up = Up(camera);
    if (Dot(direction, direction) <= 1e-12 || Dot(up, up) == 0.0)
    {
        return false;
    }
    const auto right = Cross(Normalize(up), Normalize(direction));
    return Dot(right, right) > 1e-12;
}

bool IsValid(const CameraTransform& transform) noexcept
{
    for (float value : transform.world_to_clip)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

CameraTransform BuildCameraTransform(const CameraParameters& camera, float aspect_ratio)
{
    if (!IsValid(camera) || !std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F)
    {
        throw std::invalid_argument("Invalid camera or aspect ratio.");
    }
    const auto forward = Normalize(Direction(camera));
    const auto right = Normalize(Cross(Normalize(Up(camera)), forward));
    const auto up = Cross(forward, right);
    const Vector eye{camera.eye[0], camera.eye[1], camera.eye[2]};
    const std::array<double, 16> view{
        right[0], up[0], forward[0], 0, right[1],         up[1],         forward[1],         0,
        right[2], up[2], forward[2], 0, -Dot(right, eye), -Dot(up, eye), -Dot(forward, eye), 1};
    const double half_fov = camera.vertical_fov_degrees * std::numbers::pi / 360.0;
    const double y_scale = 1.0 / std::tan(half_fov);
    const double depth_scale = static_cast<double>(camera.far_plane) /
                               (static_cast<double>(camera.far_plane) - camera.near_plane);
    const std::array<double, 16> projection{
        y_scale / aspect_ratio,           0, 0, 0, 0, y_scale, 0, 0, 0, 0, depth_scale, 1, 0, 0,
        -camera.near_plane * depth_scale, 0};
    CameraTransform result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            double value = 0;
            for (std::size_t index = 0; index < 4; ++index)
            {
                value += view[row * 4 + index] * projection[index * 4 + column];
            }
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                throw std::invalid_argument("Camera matrix exceeds finite float range.");
            }
            result.world_to_clip[row * 4 + column] = static_cast<float>(value);
        }
    }
    return result;
}
} // namespace tinyme
