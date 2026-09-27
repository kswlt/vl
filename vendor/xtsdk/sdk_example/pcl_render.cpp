#include "pcl_render.h"

int render_type = 0;
int coordType = 0;
template <>
PointCloudColorHandlerXt<pcl::PointXYZI>::PointCloudColorHandlerXt(
    const pcl::visualization::PointCloudColorHandler<pcl::PointXYZI>::PointCloudConstPtr &cloud,
    const std::string &field_name) : pcl::visualization::PointCloudColorHandler<pcl::PointXYZI>::PointCloudColorHandler(cloud),
                                     field_name_(field_name)
{
    field_idx_ = 2; // getFieldIndex ( field_name);
    if (field_idx_ != -1)
        capable_ = true;
    else
        capable_ = false;
}
#ifdef PCL_VERSION_LESS_112
template <>
bool PointCloudColorHandlerXt<pcl::PointXYZI>::getColor(vtkSmartPointer<vtkDataArray> &scalars) const
{
    // int render_type = 2;
    if (!capable_ || !cloud_)
        return false;

    float pcldistanceMin = 0;
    float pcldistanceMax = 25;
    int g_amp_range_max = 255;

    auto float_array = vtkSmartPointer<vtkFloatArray>::New();
    float_array->SetNumberOfComponents(1);

    vtkIdType nr_points = cloud_->width * cloud_->height;
    float_array->SetNumberOfTuples(nr_points); // 要不要也+200

    float *colors = new float[nr_points + 200];
    int j = 0;

    for (vtkIdType cp = 0; cp < nr_points; ++cp)
    {
        if (!std::isfinite(cloud_->points[cp].x) || !std::isfinite(cloud_->points[cp].y) || !std::isfinite(cloud_->points[cp].z))
            continue;

        if (render_type > 0)
        {
            float intensty = cloud_->points[cp].intensity;
            if (intensty > g_amp_range_max)
            {
                colors[j] = 1; // g_amp_range_max;
            }
            else
                colors[j] = g_amp_range_max - intensty;
        }
        else
        {
            // if (psdkui->coordType == 1)
            //     colors[j] = cloud_->points[cp].x;
            // else
            //     colors[j] = cloud_->points[cp].z;

            colors[j] = cloud_->points[cp].z;
        }
        j++;
    }

    for (int i = 0; i < 100; i++)
    {
        if (render_type > 0)
            colors[j] = 1;
        else
            colors[j] = pcldistanceMin; // 0.01;
        j++;
    }

    for (int i = 0; i < 100; i++)
    {
        if (render_type > 0)
            colors[j] = g_amp_range_max;
        else
            colors[j] = pcldistanceMax; // 5.00;

        // colors[j] = pcldistanceMax; // 5.00;
        j++;
    }

    float_array->SetVoidArray(colors, j, 0); // 修改这里的参数数量
    scalars = float_array;

    return true;
}

#else
template <>
vtkSmartPointer<vtkDataArray> PointCloudColorHandlerXt<pcl::PointXYZI>::getColor() const
{
    if (!capable_ || !cloud_)
        return nullptr;

    float pcldistanceMin = 0;
    float pcldistanceMax = 25;
    int g_amp_range_max = 255;

    auto scalars = vtkSmartPointer<vtkFloatArray>::New();
    scalars->SetNumberOfComponents(1);

    vtkIdType nr_points = cloud_->width * cloud_->height;
    scalars->SetNumberOfTuples(nr_points); // 要不要也+200

    float *colors = new float[nr_points + 200];
    int j = 0;

    for (vtkIdType cp = 0; cp < nr_points; ++cp)
    {
        if (!std::isfinite(cloud_->points[cp].x) || !std::isfinite(cloud_->points[cp].y) || !std::isfinite(cloud_->points[cp].z))
            continue;

        if (render_type > 0)
        {
            float intensty = cloud_->points[cp].intensity;
            if (intensty > g_amp_range_max)
            {
                colors[j] = 1; // g_amp_range_max;
            }
            else
                colors[j] = g_amp_range_max - intensty;
        }
        else
        {
            if (coordType == 1)
                colors[j] = cloud_->points[cp].x;
            else
                colors[j] = cloud_->points[cp].z;
            // colors[j] = cloud_->points[cp].z;
        }
        j++;
    }

    for (int i = 0; i < 100; i++)
    {
        if (render_type > 0)
            colors[j] = 1;
        else
            colors[j] = pcldistanceMin; // 0.01;
        j++;
    }

    for (int i = 0; i < 100; i++)
    {
        if (render_type > 0)
            colors[j] = g_amp_range_max;
        else
            colors[j] = pcldistanceMax; // 5.00;
        j++;
    }

    scalars->SetArray(colors, j, 0, vtkFloatArray::VTK_DATA_ARRAY_DELETE);

    return scalars;
}

#endif

template class PointCloudColorHandlerXt<pcl::PointXYZI>;