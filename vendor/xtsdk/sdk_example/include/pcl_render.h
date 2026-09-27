#ifndef __PCL_RENDER_H__
#define __PCL_RENDER_H__

#include <pcl/point_types.h> //点类型相关定义
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/visualization/point_cloud_geometry_handlers.h>
#include <pcl/visualization/impl/point_cloud_geometry_handlers.hpp>
#include <exception>
#include <vtkOutputWindow.h>

#include <locale>
#include <codecvt>

extern int render_type;
extern int coordType;

template <typename PointT>
class PointCloudColorHandlerXt : public pcl::visualization::PointCloudColorHandler<PointT>
{
    using PointCloud = typename pcl::visualization::PointCloudColorHandler<PointT>::PointCloud;
    using PointCloudPtr = typename PointCloud::Ptr;
    using PointCloudConstPtr = typename PointCloud::ConstPtr;

public:
    using Ptr = std::shared_ptr<PointCloudColorHandlerXt<PointT>>;
    using ConstPtr = std::shared_ptr<const PointCloudColorHandlerXt<PointT>>;

    /** \brief Constructor. */
    PointCloudColorHandlerXt(const std::string &field_name)
        : field_name_(field_name)
    {
        this->capable_ = false;
    }

    /** \brief Constructor. */
    PointCloudColorHandlerXt(const PointCloudConstPtr &cloud,
                             const std::string &field_name)
        : pcl::visualization::PointCloudColorHandler<PointT>(cloud), field_name_(field_name)
    {
        this->setInputCloud(cloud);
    }

    /** \brief Destructor. */
    virtual ~PointCloudColorHandlerXt() {}

    /** \brief Get the name of the field used. */
    virtual std::string getFieldName() const { return (field_name_); }

    // vtkSmartPointer<vtkDataArray> getColor() const override;
#ifdef PCL_VERSION_LESS_112
    bool getColor(vtkSmartPointer<vtkDataArray> &scalars) const override;
#else
    vtkSmartPointer<vtkDataArray> getColor() const override;
#endif

protected:
    /** \brief Class getName method. */
    virtual std::string getName() const { return ("PointCloudColorHandlerGenericField"); }

private:
    using pcl::visualization::PointCloudColorHandler<PointT>::cloud_;
    using pcl::visualization::PointCloudColorHandler<PointT>::capable_;
    using pcl::visualization::PointCloudColorHandler<PointT>::field_idx_;
    using pcl::visualization::PointCloudColorHandler<PointT>::fields_;

    /** \brief Name of the field used to create the color handler. */
    std::string field_name_;
};

// 模板特化声明
template <>
PointCloudColorHandlerXt<pcl::PointXYZI>::PointCloudColorHandlerXt(
    const pcl::visualization::PointCloudColorHandler<pcl::PointXYZI>::PointCloudConstPtr &cloud,
    const std::string &field_name);

#ifdef PCL_VERSION_LESS_112
template <>
bool PointCloudColorHandlerXt<pcl::PointXYZI>::getColor(vtkSmartPointer<vtkDataArray> &scalars) const;
#else
template <>
vtkSmartPointer<vtkDataArray> PointCloudColorHandlerXt<pcl::PointXYZI>::getColor() const;

#endif

#endif 
