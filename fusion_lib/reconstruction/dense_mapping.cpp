#include "dense_mapping.h"
#include "map_struct.h"
#include "cuda_utils.h"
#include "map_proc.h"

namespace fusion
{

class DenseMapping::DenseMappingImpl
{
public:
  DenseMappingImpl(IntrinsicMatrix cam_param);
  ~DenseMappingImpl();
  void update(RgbdImagePtr current_image);
  void raycast(RgbdImagePtr current_image);
  void create_scene_mesh();
  void create_scene_mesh(float3 *data, uint &max_size);
  void fetch_mesh_with_normal(float3 *vertex, float3 *normal, uint &max_size);

  IntrinsicMatrix intrinsic_matrix_;
  std::shared_ptr<MapStruct> map_struct_;

  // for raycast
  cv::cuda::GpuMat cast_vmap_;
  cv::cuda::GpuMat cast_nmap_;
  cv::cuda::GpuMat cast_image_;
  cv::cuda::GpuMat zrange_x_;
  cv::cuda::GpuMat zrange_y_;
  uint *rendering_block_count;
  RenderingBlock *rendering_block_array;

  // for map udate
  cv::cuda::GpuMat flag;
  cv::cuda::GpuMat pos_array;

  uint block_count;
  uint triangle_count;
  int3 *block_list;
  float3 *triangles;
};

DenseMapping::DenseMappingImpl::DenseMappingImpl(IntrinsicMatrix cam_param)
    : intrinsic_matrix_(cam_param)
{
  map_struct_ = std::make_shared<MapStruct>(300000, 450000, 100000, 0.003f);
  map_struct_->allocate_device_memory();
  map_struct_->reset_map_struct();

  zrange_x_.create(intrinsic_matrix_.height / 8, intrinsic_matrix_.width / 8, CV_32FC1);
  zrange_y_.create(intrinsic_matrix_.height / 8, intrinsic_matrix_.width / 8, CV_32FC1);

  safe_call(cudaMalloc(&block_list, sizeof(int3) * state.num_total_hash_entries_));
  safe_call(cudaMalloc(&triangles, sizeof(float3) * state.num_total_mesh_vertices()));
}

DenseMapping::DenseMappingImpl::~DenseMappingImpl()
{
  if (map_struct_)
    map_struct_->release_device_memory();

  safe_call(cudaFree(block_list));
  safe_call(cudaFree(triangles));
}

void DenseMapping::DenseMappingImpl::update(RgbdImagePtr current_image)
{
  RgbdFramePtr current_frame = current_image->get_reference_frame();
  if (current_frame == nullptr)
    return;

  cv::cuda::GpuMat depth = current_image->get_raw_depth();
  cv::cuda::GpuMat image = current_image->get_image();
  Sophus::SE3d pose = current_frame->get_pose();
  uint visible_block_count = 0;

  cuda::update(
      *map_struct_,
      depth, image,
      pose,
      intrinsic_matrix_,
      flag, pos_array,
      visible_block_count);
}

void DenseMapping::DenseMappingImpl::raycast(RgbdImagePtr current_image)
{
  RgbdFramePtr current_frame = current_image->get_reference_frame();
  uint visible_block_count = 0;
  map_struct_->get_visible_block_count(visible_block_count);
  if (current_frame == nullptr || visible_block_count == 0)
    return;

  Sophus::SE3d pose = current_frame->get_pose();
  cuda::create_rendering_blocks(*map_struct_, zrange_x_, zrange_y_, pose, intrinsic_matrix_);

  uint rendering_block_count = 0;
  map_struct_->get_rendering_block_count(rendering_block_count);

  if (rendering_block_count != 0)
  {
    cast_vmap_ = current_image->get_vmap();
    cast_nmap_ = current_image->get_nmap();
    cast_image_ = current_image->get_image();

    // cuda::raycast_with_colour(
    //     *map_struct_,
    //     cast_vmap_,
    //     cast_nmap_,
    //     cast_image_,
    //     zrange_x_,
    //     zrange_y_,
    //     pose,
    //     intrinsic_matrix_);

    cuda::raycast(
        *map_struct_,
        cast_vmap_,
        cast_nmap_,
        zrange_x_,
        zrange_y_,
        pose,
        intrinsic_matrix_);

    // cv::Mat img(cast_image_);
    // cv::resize(img, img, cv::Size2i(0, 0), 2, 2);
    // cv::imshow("img", img);
    // cv::waitKey(1);
  }
}

void DenseMapping::DenseMappingImpl::create_scene_mesh()
{
  cuda::create_scene_mesh(*map_struct_, block_count, block_list, triangle_count, triangles);
}

void DenseMapping::DenseMappingImpl::create_scene_mesh(float3 *data, uint &max_size)
{
  cuda::create_scene_mesh(*map_struct_, block_count, block_list, max_size, data);
}

void DenseMapping::DenseMappingImpl::fetch_mesh_with_normal(float3 *vertex, float3 *normal, uint &max_size)
{
  cuda::create_scene_mesh_with_normal(*map_struct_, block_count, block_list, max_size, vertex, normal);
}

DenseMapping::DenseMapping(IntrinsicMatrix cam_param) : impl(new DenseMappingImpl(cam_param))
{
}

void DenseMapping::update(RgbdImagePtr image)
{
  impl->update(image);
}

void DenseMapping::raycast(RgbdImagePtr image)
{
  impl->raycast(image);
}

void DenseMapping::create_scene_mesh()
{
  impl->create_scene_mesh();
}

void DenseMapping::create_scene_mesh(float3 *data, uint &max_size)
{
  impl->create_scene_mesh(data, max_size);
}

void DenseMapping::fetch_mesh_with_normal(float3 *vertex, float3 *normal, uint &max_size)
{
  impl->fetch_mesh_with_normal(vertex, normal, max_size);
}

void DenseMapping::restart_mapping()
{
  impl->map_struct_->reset_map_struct();
}

// Write mesh to a stl file
void DenseMapping::write_mesh_to_file(const char *file_name)
{
  uint host_triangle_count = impl->triangle_count;
  if (host_triangle_count == 0)
    return;

  float3 *host_triangles = (float3 *)malloc(sizeof(float3) * host_triangle_count * 3);
  safe_call(cudaMemcpy(host_triangles, impl->triangles, sizeof(float3) * host_triangle_count * 3, cudaMemcpyDeviceToHost));

  FILE *f = fopen(file_name, "wb+");

  if (f != NULL)
  {
    for (int i = 0; i < 80; i++)
      fwrite(" ", sizeof(char), 1, f);

    fwrite(&host_triangle_count, sizeof(int), 1, f);

    float zero = 0.0f;
    short attribute = 0;
    for (uint i = 0; i < host_triangle_count; i++)
    {
      fwrite(&zero, sizeof(float), 1, f);
      fwrite(&zero, sizeof(float), 1, f);
      fwrite(&zero, sizeof(float), 1, f);

      fwrite(&host_triangles[i * 3].x, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3].y, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3].z, sizeof(float), 1, f);

      fwrite(&host_triangles[i * 3 + 1].x, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3 + 1].y, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3 + 1].z, sizeof(float), 1, f);

      fwrite(&host_triangles[i * 3 + 2].x, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3 + 2].y, sizeof(float), 1, f);
      fwrite(&host_triangles[i * 3 + 2].z, sizeof(float), 1, f);

      fwrite(&attribute, sizeof(short), 1, f);
    }

    fclose(f);
  }

  delete host_triangles;
}

} // namespace fusion