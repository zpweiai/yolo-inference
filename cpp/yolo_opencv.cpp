#include "yolo_opencv.h"
#include "utils.h"


void YOLO_OpenCV::init(const Algo_Type algo_type, const Device_Type device_type, const Model_Type model_type, const std::string model_path)
{
	if (algo_type != YOLOv5 && algo_type != YOLOv8)
	{
		std::cout << "unsupported algo type!" << std::endl;
		std::exit(-1);
	}
	m_algo = algo_type;
	if (m_algo == YOLOv5)
	{
		m_output_numprob = 5 + num_classes;
		m_output_numbox = 3 * (input_width / 8 * input_height / 8 + input_width / 16 * input_height / 16 + input_width / 32 * input_height / 32);
		m_output_numel = 1 * m_output_numprob * m_output_numbox;
	}
	if (m_algo == YOLOv8)
	{
		m_output_numprob = 4 + num_classes;
		m_output_numbox = input_width / 8 * input_height / 8 + input_width / 16 * input_height / 16 + input_width / 32 * input_height / 32;
		m_output_numel = 1 * m_output_numprob * m_output_numbox;
	}

	if (model_type != FP32 && model_type != FP16)
	{
		std::cout << "unsupported model type!" << std::endl;
		std::exit(-1);
	}
	m_net = cv::dnn::readNet(model_path);
	if (device_type == CPU)
	{
		m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
		m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
	}
	if (device_type == GPU)
	{
		m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
		if (model_type == FP32)
		{
			m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
		}
		if (model_type == FP16)
		{
			m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA_FP16);
		}
	}
}


void YOLO_OpenCV::pre_process()
{
	cv::Vec4d params;
	cv::Mat letterbox;
	LetterBox(m_image, letterbox, cv::Size(input_width, input_height));
	cv::dnn::blobFromImage(letterbox, m_inputs, 1. / 255., cv::Size(input_width, input_height), cv::Scalar(), true, false);
}


void YOLO_OpenCV::process()
{
	m_net.setInput(m_inputs);
	m_net.forward(m_outputs, m_net.getUnconnectedOutLayersNames());
	m_outputs_host = (float*)m_outputs[0].data;
}


void YOLO_OpenCV::post_process()
{
	std::vector<cv::Rect> boxes;
	std::vector<float> scores;
	std::vector<int> class_ids;

	for (int i = 0; i < m_output_numbox; ++i)
	{
		float* ptr = m_outputs_host + i * m_output_numprob;
		int class_id;
		float score;
		if (m_algo == YOLOv5)
		{
			float objness = ptr[4];
			if (objness < confidence_threshold)
				continue;
			float* classes_scores = ptr + 5;
			class_id = std::max_element(classes_scores, classes_scores + num_classes) - classes_scores;
			score = classes_scores[class_id] * objness;
		}
		if (m_algo == YOLOv8)
		{
			float* classes_scores = ptr + 4;
			class_id = std::max_element(classes_scores, classes_scores + num_classes) - classes_scores;
			score = classes_scores[class_id];
		}
		if (score < score_threshold)
			continue;

		float x = ptr[0];
		float y = ptr[1];
		float w = ptr[2];
		float h = ptr[3];
		int left = int(x - 0.5 * w);
		int top = int(y - 0.5 * h);
		int width = int(w);
		int height = int(h);

		cv::Rect box = cv::Rect(left, top, width, height);
		scale_box(box, m_image.size());
		boxes.push_back(box);
		scores.push_back(score);
		class_ids.push_back(class_id);
	}

	std::vector<int> indices;
	nms(boxes, scores, score_threshold, nms_threshold, indices);
	for (int i = 0; i < indices.size(); i++)
	{
		int idx = indices[i];
		cv::Rect box = boxes[idx];
		std::string label = class_names[class_ids[idx]] + ":" + cv::format("%.2f", scores[idx]);
		draw_result(m_result, label, box);
	}
}