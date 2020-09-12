#include "PMDCamera.h"

bool SAVEPOINTCLOUD = false;
std::mutex pointcloud_mutex;

PMDCamera::PMDCamera(pcl::visualization::PCLVisualizer::Ptr viewer_ptr)
{
	m_listener_point_cloud.set_viewer_ptr(viewer_ptr);
}


PMDCamera::~PMDCamera()
{
}

int PMDCamera::get_camera_size(size_t &camera_size)
{
	m_camera_list = m_camera_manager.getConnectedCameraList();

	camera_size =  m_camera_list.size();

	return 0;
}

int PMDCamera::init_camera(size_t camera_index, size_t operate_mode)
{
	m_camera_device = m_camera_manager.createCamera(m_camera_list[camera_index]);

	royale::CameraStatus status = m_camera_device->initialize();

	if (status != CameraStatus::SUCCESS)
	{
		cerr << "Cannot initialize the camera device, error string : " << getErrorString(status) << endl;
		return 1;
	}

	royale::Vector<royale::String> useCases;

	status = m_camera_device->getUseCases(useCases);

	if (status != royale::CameraStatus::SUCCESS || useCases.empty())
	{
		cerr << "No use cases are available" << endl;
		cerr << "getUseCases() returned: " << getErrorString(status) << endl;
		return 1;
	}

	for (size_t i = 0; i < useCases.size(); ++i)
	{
		if (i == operate_mode)
		{
			cout << "Select the " << useCases[i] << "mode" << endl;
		}
	}
	// set an operation mode
	if (m_camera_device->setUseCase(useCases.at(operate_mode)) != royale::CameraStatus::SUCCESS)
	{
		cerr << "Error setting use case" << endl;
		return 1;
	}
	return 0;
}

int PMDCamera::set_camera_data_mode(size_t data_mode)
{
	if (data_mode == 0)
	{
		if (m_camera_device->registerSparsePointCloudListener(&m_listener_point_cloud) != CameraStatus::SUCCESS)
		{
			cerr << "Error registering point cloud listener" << endl;
			return 1;
		}
	}

	else if (data_mode == 1)
	{
		if (m_camera_device->registerDepthImageListener(&m_listener_depth) != CameraStatus::SUCCESS)
		{
			cerr << "Error registering depth listener" << endl;
			return 1;
		}
	}
	return 0;
}

int PMDCamera::start_capture()
{
	this->stop_capture();

	if (m_camera_device->startCapture() != CameraStatus::SUCCESS)
	{
		cerr << "Error starting the capturing" << endl;
		return 1;
	}
	return 0;
}

int PMDCamera::stop_capture()
{
	if (m_camera_device->stopCapture() != CameraStatus::SUCCESS)
	{
		cerr << "Error stopping the capturing" << endl;
		return 1;
	}
	return 0;
}

pcl::PointCloud<PCFORMAT>::Ptr PMDCamera::get_visualization_cloud_ptr()
{
	return m_listener_point_cloud.get_cloud_ptr()[0];
}

void PMDCamera::set_saving_type(const std::string type)
{
	m_listener_point_cloud.set_saving_type(type);
}

void PMDCamera::set_capture_range(float min_x, float max_x, float min_y, float max_y, float min_z, float max_z)
{
	m_listener_point_cloud.set_capture_range(min_x, max_x, min_y, max_y, min_z, max_z);
}

void ListenerPointCloud::save_royale_xyzcPoints(const royale::SparsePointCloud * data)
{
	m_cloud_ptr_vec[1]->clear();

	PCFORMAT p;
	for (size_t i = 0; i < data->xyzcPoints.size(); i += 4)
	{
		p.x = data->xyzcPoints.at(i),
		p.y = data->xyzcPoints.at(i + 1),
		p.z = data->xyzcPoints.at(i + 2),
		p.intensity = data->xyzcPoints.at(i + 3);

		m_cloud_ptr_vec[1]->points.push_back(p);
	}
	m_cloud_ptr_vec[1]->width = m_cloud_ptr_vec[1]->points.size();
	m_cloud_ptr_vec[1]->height = 1;
}

void ListenerPointCloud::filter_point_cloud()
{
	pcl::PassThrough<PCFORMAT> pass;
	pass.setInputCloud(m_cloud_ptr_vec[1]);
	pass.setFilterFieldName("x");
	pass.setFilterLimits(m_capture_range[0], m_capture_range[1]);
	
	pass.setFilterFieldName("y");
	pass.setFilterLimits(m_capture_range[2], m_capture_range[3]);

	pass.setFilterFieldName("z");
	pass.setFilterLimits(m_capture_range[4], m_capture_range[5]);

	//pass.setFilterLimitsNegative (true);
	pass.filter(*m_cloud_ptr_vec[1]);
}

ListenerPointCloud::ListenerPointCloud()
{
	m_cloud_ptr_vec.resize(2, boost::make_shared<pcl::PointCloud<PCFORMAT>>());
	
	m_frame_count = 0;

	m_saving_type = "bin";
}


ListenerPointCloud::~ListenerPointCloud()
{

}

void ListenerPointCloud::set_viewer_ptr(pcl::visualization::PCLVisualizer::Ptr viewer_ptr)
{
	m_viewer_ptr = viewer_ptr;
}

void ListenerPointCloud::onNewData(const royale::SparsePointCloud * data)
{
	std::lock_guard<std::mutex> guard(pointcloud_mutex);
	// save to m_cloud_ptr_vec[1]
	save_royale_xyzcPoints(data);

	filter_point_cloud();

	if (SAVEPOINTCLOUD)
	{
		std::string save_filename = get_current_date();
		if (m_saving_type == "bin")
		{
			save_filename = save_filename + ".bin";
			write_point_cloud_binary(m_cloud_ptr_vec[1], save_filename);
		}
		else if (m_saving_type == "txt")
		{
			save_filename = save_filename + ".pcd";
			write_point_cloud_acsii(m_cloud_ptr_vec[1], save_filename);
		}
		std::cout << "[" << ++m_frame_count << "]" << "write to " << save_filename << "(" << m_cloud_ptr_vec[1]->points.size() << ")" << std::endl;
		SAVEPOINTCLOUD = false;
	}
	std::swap(m_cloud_ptr_vec[0], m_cloud_ptr_vec[1]);

	//Sleep(200);
}

std::vector<pcl::PointCloud<PCFORMAT>::Ptr>& ListenerPointCloud::get_cloud_ptr()
{
	return m_cloud_ptr_vec;
}

void ListenerPointCloud::set_saving_type(const std::string & type)
{
	m_saving_type = type;
}

void ListenerPointCloud::set_capture_range(float min_x, float max_x, float min_y, float max_y, float min_z, float max_z)
{
	m_capture_range.push_back(min_x);
	m_capture_range.push_back(max_x);
	
	m_capture_range.push_back(min_y);
	m_capture_range.push_back(max_y);

	m_capture_range.push_back(min_z);
	m_capture_range.push_back(max_z);
}

ListenerDepth::ListenerDepth()
{
}

ListenerDepth::~ListenerDepth()
{
}

void ListenerDepth::onNewData(const royale::DepthImage * data)
{
	cout << "retrieve depth image ..." << endl;
}

void add_point_cloud_visualization(pcl::visualization::PCLVisualizer::Ptr viewer_ptr, pcl::PointCloud<PCFORMAT>::Ptr m_cloud_ptr)
{
	if (!m_cloud_ptr->points.empty())
	{
		// Keep the same name as shown in main function
		pcl::visualization::PointCloudColorHandlerGenericField<PCFORMAT> fildColor(m_cloud_ptr, "z");
		viewer_ptr->updatePointCloud<PCFORMAT>(m_cloud_ptr, fildColor, "cloud");
	}
}

void write_point_cloud_binary(pcl::PointCloud<PCFORMAT>::Ptr m_cloud_ptr, const std::string filename)
{
	ofstream fout(filename.c_str(), ios::out | ios::binary);
	fout.write((char *)m_cloud_ptr->points.data(), sizeof(PCFORMAT) * m_cloud_ptr->points.size());
	fout.close();
}

void write_point_cloud_acsii(pcl::PointCloud<PCFORMAT>::Ptr m_cloud_ptr, const std::string filename)
{
	if (!m_cloud_ptr->points.empty())
		pcl::io::savePCDFile(filename, *m_cloud_ptr);
}

std::string get_current_date()
{
	time_t tt = time(NULL);
	struct tm *stm = localtime(&tt);

	char tmp[32];
	sprintf(tmp, "%04d-%02d-%02d-%02d-%02d-%02d", 1900 + stm->tm_year, 1 + stm->tm_mon, stm->tm_mday, stm->tm_hour,
		stm->tm_min, stm->tm_sec);

	return std::string(tmp);
}
