#include <tinyme/core/Camera.h>
#include <tinyme/core/LaunchOptions.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

namespace
{
int checks = 0;
void Check(bool value, const char* description)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(description);
    }
}
void Near(double value, double expected, const char* description)
{
    Check(std::isfinite(value) && std::abs(value - expected) < 1e-5, description);
}
std::array<double, 3> Project(const tinyme::CameraTransform& transform,
                              const std::array<double, 3>& position)
{
    std::array<double, 4> clip{};
    const std::array<double, 4> point{position[0], position[1], position[2], 1};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            clip[column] += point[row] * transform.world_to_clip[row * 4 + column];
        }
    }
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}
void RejectCamera(const tinyme::CameraParameters& camera)
{
    Check(!tinyme::IsValid(camera), "Invalid camera fails validation");
    bool rejected = false;
    try
    {
        static_cast<void>(tinyme::BuildCameraTransform(camera, 1.0F));
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "Invalid camera rejected before normalization");
}
void TestProjection()
{
    tinyme::CameraParameters camera;
    Check(tinyme::IsValid(camera), "Default camera valid");
    const tinyme::CameraTransform identity;
    Check(tinyme::IsValid(identity), "Default transform identity finite");
    for (std::size_t index = 0; index < 16; ++index)
    {
        Near(identity.world_to_clip[index], index % 5 == 0 ? 1 : 0, "Identity layout");
    }
    const auto matrix = tinyme::BuildCameraTransform(camera, 2.0F);
    Check(tinyme::IsValid(matrix), "Constructed transform finite");
    Near(matrix.world_to_clip[0], 0.5, "Horizontal scale includes aspect");
    Near(matrix.world_to_clip[5], 1, "Vertical FOV uses degrees");
    Near(matrix.world_to_clip[11], 1, "Perspective w uses view depth");
    Near(matrix.world_to_clip[15], 2, "Translation occupies row, not column");
    const auto origin = Project(matrix, {0, 0, 0});
    Near(origin[0], 0, "Default target projects to horizontal center");
    Near(origin[1], 0, "Default target projects to vertical center");
    Near(origin[2],
         (2.0 - camera.near_plane) * camera.far_plane /
             (2.0 * (camera.far_plane - camera.near_plane)),
         "D3D perspective depth");
    const auto near = Project(matrix, {0, 0, -2.0 + camera.near_plane});
    const auto far = Project(matrix, {0, 0, -2.0 + camera.far_plane});
    Near(near[2], 0, "Near plane maps to zero");
    Near(far[2], 1, "Far plane maps to one");
    const auto top = Project(matrix, {0, 1.3, 0});
    Near(top[1], 0.65, "World triangle preserves reference vertical extent");
    const auto square = tinyme::BuildCameraTransform(camera, 1.0F);
    Near(Project(square, {1, 0, 0})[0], 0.5, "Square viewport horizontal extent");
    Near(Project(matrix, {1, 0, 0})[0], 0.25, "Wide viewport keeps geometry proportions");
    camera.eye = {2, 3, -4};
    camera.target = {2, 3, 0};
    const auto translated = tinyme::BuildCameraTransform(camera, 2.0F);
    const auto point = Project(translated, {3, 5, 0});
    Near(point[0], 0.125, "Non-symmetric translation and perspective order X");
    Near(point[1], 0.5, "Non-symmetric translation and perspective order Y");
    Near(translated.world_to_clip[12], -1, "Row-major translation X scaled by projection");
    Near(translated.world_to_clip[13], -3, "Row-major translation Y");
    camera = {};
    camera.eye = {2, 0, 0};
    const auto rotated = Project(tinyme::BuildCameraTransform(camera, 1.0F), {0, 1, 1});
    Near(rotated[0], 0.5, "Left-handed ninety-degree view rotation X");
    Near(rotated[1], 0.5, "Left-handed ninety-degree view rotation Y");
    camera = {};
    camera.up = {1, 1, 0};
    const auto rolled = Project(tinyme::BuildCameraTransform(camera, 1.0F), {1, 1, 0});
    Near(rolled[0], 0, "Non-axis up direction is orthonormalized");
    Near(rolled[1], std::sqrt(0.5), "Camera roll uses correct vector multiplication");
    camera = {};
    camera.eye[2] = -4;
    Near(Project(tinyme::BuildCameraTransform(camera, 1.0F), {1, 0, 0})[0], 0.25,
         "Farther eye shrinks projection");
    camera = {};
    camera.vertical_fov_degrees = 60;
    Near(Project(tinyme::BuildCameraTransform(camera, 1.0F), {1, 0, 0})[0], std::sqrt(3.0) / 2.0,
         "Narrower FOV enlarges projection");
}
void TestValidation()
{
    for (float fov : {10.0F, 120.0F})
    {
        tinyme::CameraParameters camera;
        camera.vertical_fov_degrees = fov;
        Check(tinyme::IsValid(camera), "Inclusive FOV boundary");
        Check(tinyme::IsValid(tinyme::BuildCameraTransform(camera, 1.0F)),
              "Boundary FOV finite matrix");
    }
    tinyme::CameraParameters bounds;
    bounds.eye = {-10000, -10000, -10000};
    bounds.target = {10000, 10000, 10000};
    bounds.near_plane = 0.001F;
    bounds.far_plane = 100000;
    Check(tinyme::IsValid(bounds) && tinyme::IsValid(tinyme::BuildCameraTransform(bounds, 0.5F)),
          "Coordinate and clipping bounds use safe internal precision");
    bounds.near_plane = 1000;
    Check(tinyme::IsValid(bounds), "Inclusive near-plane maximum");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -inf, 10000.1F, -10000.1F})
    {
        auto camera = tinyme::CameraParameters{};
        camera.eye[0] = bad;
        RejectCamera(camera);
        camera = {};
        camera.target[1] = bad;
        RejectCamera(camera);
    }
    for (float bad : {nan, inf, 0.0F, 9.99F, 120.01F})
    {
        auto camera = tinyme::CameraParameters{};
        camera.vertical_fov_degrees = bad;
        RejectCamera(camera);
    }
    for (float bad : {nan, inf, 0.0F, 0.0009F, 1000.1F})
    {
        auto camera = tinyme::CameraParameters{};
        camera.near_plane = bad;
        RejectCamera(camera);
    }
    for (float bad : {nan, inf, 0.0F, 0.1F, 0.1005F, 100001.0F})
    {
        auto camera = tinyme::CameraParameters{};
        camera.far_plane = bad;
        RejectCamera(camera);
    }
    for (const auto up : {std::array<float, 3>{0, 0, 0}, std::array<float, 3>{0, 0, 1},
                          std::array<float, 3>{0, 0, -1}, std::array<float, 3>{nan, 1, 0},
                          std::array<float, 3>{0, inf, 0}})
    {
        auto camera = tinyme::CameraParameters{};
        camera.up = up;
        RejectCamera(camera);
    }
    auto camera = tinyme::CameraParameters{};
    camera.eye = camera.target;
    RejectCamera(camera);
    camera.eye[2] = -1e-7F;
    RejectCamera(camera);
    camera = {};
    camera.up = {0, 5e-7F, 1};
    RejectCamera(camera);
    camera.up = {0, 2e-6F, 1};
    Check(tinyme::IsValid(camera), "Near-parallel up outside rejection threshold is valid");
    Check(tinyme::IsValid(tinyme::BuildCameraTransform(camera, 1.0F)),
          "Near-parallel but valid up produces finite matrix");
    camera = {};
    camera.up = {0, 1e-30F, 0};
    Check(tinyme::IsValid(camera), "Small nonzero up vector normalized in double precision");
    Check(tinyme::IsValid(tinyme::BuildCameraTransform(camera, 1.0F)),
          "Tiny up gives finite matrix");
    for (float aspect : {0.0F, -1.0F, nan, inf, std::numeric_limits<float>::denorm_min()})
    {
        bool rejected = false;
        try
        {
            static_cast<void>(tinyme::BuildCameraTransform({}, aspect));
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Check(rejected, "Invalid or unrepresentable aspect rejected");
    }
    auto transform = tinyme::CameraTransform{};
    transform.world_to_clip[7] = nan;
    Check(!tinyme::IsValid(transform), "Nonfinite transform rejected");
}
void TestCli()
{
    const auto parse = [](std::initializer_list<std::string_view> arguments)
    { return tinyme::ParseCommandLine(std::span(arguments.begin(), arguments.size())); };
    const auto valid =
        parse({"--windowed", "--camera-eye", "0.25", "0.125", "-3", "--camera-target", "0.25",
               "0.125", "0", "--fov", "75", "--profile", "--warmup", "0", "--samples", "1"});
    Check(valid.IsValid(), "Camera CLI combines with existing profile budget");
    Check(valid.options.camera.eye == std::array<float, 3>{0.25F, 0.125F, -3} &&
              valid.options.camera.target == std::array<float, 3>{0.25F, 0.125F, 0} &&
              valid.options.camera.vertical_fov_degrees == 75 && valid.options.frame_count == 1,
          "CLI camera components and budget preserved");
    for (std::string_view fov : {"10", "120"})
    {
        Check(parse({"--windowed", "--fov", fov}).IsValid(), "CLI inclusive FOV bounds");
    }
    Check(!parse({"--headless", "--camera-eye", "0", "0", "-2"}).IsValid(),
          "Headless rejects camera");
    Check(!parse({"--windowed", "--experiment", "clear", "--fov", "90"}).IsValid(),
          "Clear rejects camera");
    Check(!parse({"--windowed", "--camera-eye", "0", "0"}).IsValid(),
          "Camera missing component rejected");
    Check(!parse({"--windowed", "--camera-target", "0", "0", "0", "--camera-target", "0", "0", "0"})
               .IsValid(),
          "Duplicate target rejected");
    Check(!parse({"--windowed", "--camera-eye", "0", "0", "-2", "--camera-eye", "0", "0", "-3"})
               .IsValid(),
          "Duplicate eye rejected");
    Check(!parse({"--windowed", "--fov", "90", "--fov", "90"}).IsValid(), "Duplicate FOV rejected");
    Check(!parse({"--windowed", "--camera-eye", "0", "0", "0"}).IsValid(),
          "CLI degenerate view rejected");
    Check(!parse({"--windowed", "--camera-eye", "0", "-2", "0"}).IsValid(),
          "CLI parallel up rejected");
    for (std::string_view bad : {"nan", "inf", "90x", "", "9", "121", "1e100"})
    {
        Check(!parse({"--windowed", "--fov", bad}).IsValid(), "Malformed or invalid FOV rejected");
    }
    for (std::string_view bad : {"nan", "inf", "1x", "", "10001", "-10001", "1e100"})
    {
        Check(!parse({"--windowed", "--camera-target", bad, "0", "0"}).IsValid(),
              "Malformed or invalid coordinates rejected");
    }
}
} // namespace
int main()
{
    try
    {
        TestProjection();
        TestValidation();
        TestCli();
        std::cout << "camera_contracts checks=" << checks << " failures=0 no_gpu=1\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
