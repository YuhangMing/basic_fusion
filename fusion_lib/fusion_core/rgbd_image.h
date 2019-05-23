#ifndef __RGBD_FRAME__
#define __RGBD_FRAME__

#include <memory>
#include <Eigen/Core>
#include <sophus/se3.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/cudaarithm.hpp>
#include "intrinsic_matrix.h"

namespace fusion
{

class RgbdImage;
class RgbdFrame;
typedef std::shared_ptr<RgbdImage> RgbdImagePtr;
typedef std::shared_ptr<RgbdFrame> RgbdFramePtr;

class RgbdImage
{
public:
  RgbdImage();
  RgbdImage(const RgbdImage &) = delete;
  RgbdImage(const int &max_level);

  void resize_device_map();
  void upload(const RgbdFramePtr frame, const IntrinsicMatrixPyramidPtr intrinsics_pyr);

  RgbdFramePtr get_reference_frame() const;
  cv::cuda::GpuMat get_rendered_image() const;
  cv::cuda::GpuMat get_rendered_scene_textured() const;
  cv::cuda::GpuMat get_depth(const int &level = 0) const;
  cv::cuda::GpuMat get_raw_depth() const;
  cv::cuda::GpuMat get_image(const int &level = 0) const;
  cv::cuda::GpuMat get_vmap(const int &level = 0) const;
  cv::cuda::GpuMat get_nmap(const int &level = 0) const;
  cv::cuda::GpuMat get_intensity(const int &level = 0) const;
  cv::cuda::GpuMat get_intensity_dx(const int &level = 0) const;
  cv::cuda::GpuMat get_intensity_dy(const int &level = 0) const;

private:
  class RgbdImageImpl;
  std::shared_ptr<RgbdImageImpl> impl;
};

class RgbdFrame
{
public:
  ~RgbdFrame();

  // delete default and copy constructors
  RgbdFrame() = delete;
  RgbdFrame(const RgbdFrame &) = delete;
  RgbdFrame &operator=(const RgbdFrame &) = delete;

  // create new rgbd frame
  RgbdFrame(const cv::Mat &depth, const cv::Mat &image, size_t id, double ts);

  // get frame id
  size_t get_id() const;

  // get source image : CV_8UC3
  cv::Mat get_image() const;

  // get source depth : CV_32FC1
  cv::Mat get_depth() const;

  // get current pose in SE3d
  Sophus::SE3d get_pose() const;

  // get reference frame
  RgbdFramePtr get_reference_frame() const;

  // set current pose
  void set_pose(const Sophus::SE3d &pose);

  // set reference frame
  void set_reference_frame(RgbdFramePtr reference);

private:
  cv::Mat source_image;
  cv::Mat source_depth;
  cv::Mat vmap;
  cv::Mat nmap;

  size_t frame_id;
  double time_stamp;
  Sophus::SE3d pose;
  RgbdFramePtr reference;
};

} // namespace fusion

#endif