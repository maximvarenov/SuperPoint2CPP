#include "test.hpp"

namespace NAMU_TEST
{

    // #########################  DEFINITION  ###############################

    /*
     A distinction is made between three kinds of persistent data 
    that may be associated with a Module:

      1. Parameters: tensors that record gradients, typically weights 
        updated during the backward step (e.g. the weight of a Linear module),

      2. Buffers: tensors that do not record gradients, typically updated 
        during the forward step, such as running statistics (e.g. mean and variance in the BatchNorm module),

      3. Any additional state, not necessarily tensors, 
        required for the implementation or configuration of a Module.
    
     Lastly, registered parameters and buffers are handled specially during a clone() operation,
    which performs a deepcopy of a cloneable Module hierarchy.

    */

    SuperPoint::SuperPoint()
    {
        /* 
            A Module is registered as a submodule to another Module 
            by calling register_module(), typically from within a parent 
            module’s constructor.
        */

        //SHARED ENCODER
        conv1a = register_module("conv1a", Conv2d(Conv2dOptions(1, c1, 3).stride(1).padding(1)));
        conv1b = register_module("conv1b", Conv2d(Conv2dOptions(c1, c1, 3).stride(1).padding(1)));

        conv2a = register_module("conv2a", Conv2d(Conv2dOptions(c1, c2, 3).stride(1).padding(1)));
        conv2b = register_module("conv2b", Conv2d(Conv2dOptions(c2, c2, 3).stride(1).padding(1)));

        conv3a = register_module("conv3a", Conv2d(Conv2dOptions(c2, c3, 3).stride(1).padding(1)));
        conv3b = register_module("conv3b", Conv2d(Conv2dOptions(c3, c3, 3).stride(1).padding(1)));

        conv4a = register_module("conv4a", Conv2d(Conv2dOptions(c3, c4, 3).stride(1).padding(1)));
        conv4b = register_module("conv4b", Conv2d(Conv2dOptions(c4, c4, 3).stride(1).padding(1)));

        //DETECTOR
        convPa = register_module("convPa", Conv2d(Conv2dOptions(c4, c5, 3).stride(1).padding(1)));
        convPb = register_module("convPb", Conv2d(Conv2dOptions(c5, 65, 1).stride(1).padding(0)));

        //DESCRIPTOR
        convDa = register_module("convDa", Conv2d(Conv2dOptions(c4, c5, 3).stride(1).padding(1)));
        convDb = register_module("convDb", Conv2d(Conv2dOptions(c5, d1, 1).stride(1).padding(0)));
    }

    std::vector<Tensor> SuperPoint::forward(torch::Tensor x)
    {
        //SHARED ENCODER
        x = relu(conv1a->forward(x));
        x = relu(conv1b->forward(x));
        x = max_pool2d(x, 2, 2);

        x = relu(conv2a->forward(x));
        x = relu(conv2b->forward(x));
        x = max_pool2d(x, 2, 2);

        x = relu(conv3a->forward(x));
        x = relu(conv3b->forward(x));
        x = max_pool2d(x, 2, 2);

        x = relu(conv4a->forward(x));
        x = relu(conv4b->forward(x));

        //DETECTOR
        auto cPa = relu(convPa->forward(x));
        auto semi = convPb->forward(cPa); // [B, 65, H/8, W/8]

        //DESCRIPTOR
        auto cDa = relu(convDa->forward(x));
        auto desc = convDb->forward(cDa); // [B, 256, H/8, W/8]

        if (Explain)
            desc.print();
        auto dn = norm(desc, 2, 1);
        if (Explain)
            dn.print();
        desc = desc.div(unsqueeze(dn, 1));
        if (Explain)
            desc.print();

        if (Explain)
            semi.print();
        semi = softmax(semi, 1);
        if (Explain)
            semi.print();
        semi = semi.slice(1, 0, 64);
        if (Explain)
            semi.print();
        semi = semi.permute({0, 2, 3, 1}); // [B, H/8, W/8, 64]
        if (Explain)
            semi.print();

        int Hc = semi.size(1);
        int Wc = semi.size(2);
        semi = semi.contiguous().view({-1, Hc, Wc, 8, 8});
        if (Explain)
            semi.print();
        semi = semi.permute({0, 1, 3, 2, 4});
        if (Explain)
            semi.print();
        semi = semi.contiguous().view({-1, Hc * 8, Wc * 8}); // [B, H, W]
        if (Explain)
            semi.print();

        std::vector<Tensor> ret;
        ret.push_back(semi);
        ret.push_back(desc);

        return ret;
    }

    void display()
    { // Display some information.
        // 1. Cuda Availability. && 2. GPU number.
        // 3. cudnn availability.

        bool use_cuda = torch::cuda::is_available();
        std::cout << "torch::cuda::is_available()\n";
        std::cout << "My Device Type is " << (use_cuda ? "Cuda!" : "CPU!") << std::endl;

        torch::Tensor tensor = torch::rand({2, 3}).cuda();
        // std::cout << tensor << std::endl;
        tensor.print();
        std::cout << "I have " << torch::cuda::device_count() << " GPUs.\n";
        std::cout << "cudnn is " << (torch::cuda::cudnn_is_available() ? "available" : "unavailable") << std::endl;
    }
    void display(std::shared_ptr<SuperPoint> net)
    {
        // Display Module and Submodule's detail informations.
        // Whether it is trainable && module's name.
        std::cout.setf(std::ios::left);
        std::cout << "\n[ " << net->name() << " ]\n";
        std::cout << std::setw(12) << "Trainable" << (net->is_training() ? "On\n\n" : "Off\n\n");

        //auto subnets = net->children();
        for (auto subnet : net->children())
        {
            subnet->pretty_print(std::cout);
            std::cout << '\n';
            std::cout << std::setw(12) << "Trainable"
                      << (subnet->is_training() ? "On\n\n" : "Off\n\n");
        }
    }

    SuperPointFrontend::SuperPointFrontend(std::string _weight_dir, bool _use_cuda)
    {
        model = std::make_shared<SuperPoint>();
        torch::load(model, _weight_dir);

        device_type = (use_cuda) ? kCUDA : kCPU;
        c10::Device device(device_type);

        tensor_opts = torch::TensorOptions()
                          .dtype(torch::kFloat32)
                          .layout(torch::kStrided)
                          .device(device)
                          .requires_grad(false);
        if (_use_cuda)
            model->to(device);
        model->eval();
    }

    cv::Mat SuperPointFrontend::detect(cv::Mat &img)
    {
        device_type = (use_cuda) ? kCUDA : kCPU;
        c10::Device device(device_type);

        auto x = torch::from_blob(img.clone().data, {1, 1, img.rows, img.cols}, tensor_opts);
        x /= 255;
        auto out = model->forward(x);
        auto prob = mProb = out[0].squeeze(0); // [H, W]
        auto desc = mDesc = out[1];            // [1, 256, H/8, W/8]

        // Nonzero인 좌표를 Tensor로 저장.
        auto kpts = (prob > conf_thres);
        kpts = at::nonzero(kpts); // [n_keypoints, 2]  (y, x)

        std::cout << "kpyts' type is ";
        kpts.print();
        std::cout << std::endl;
        auto fkpts = kpts.to(kFloat);

        auto grid = torch::zeros({1, 1, kpts.size(0), 2}).to(device);               // [1, 1, n_keypoints, 2]
        grid[0][0].slice(1, 0, 1) = 2.0 * fkpts.slice(1, 1, 2) / mProb.size(1) - 1; // x
        grid[0][0].slice(1, 1, 2) = 2.0 * fkpts.slice(1, 0, 1) / mProb.size(0) - 1; // y

        //  torch.nn.functional.grid_sample(input, grid, mode='bilinear',
        //                            padding_mode='zeros', align_corners=None)
        //  : Given an input and a flow-field grid, computes the output
        //    using input values and pixel locations from grid.
        desc = at::grid_sampler(desc, grid, 0, 0, false); // [1, 256, 1, n_keypoints]       //CHANGED
        desc = desc.squeeze(0).squeeze(1);                // [256, n_keypoints]

        // normalize to dim 1 with 2-Norm.
        // 각 키포인트에 대해서 Normalize.
        auto dn = norm(desc, 2, 1);        // [256, 1]
        desc = desc.div(unsqueeze(dn, 1)); // [256, n_keypoints]

        desc = desc.transpose(0, 1).contiguous(); // [n_keypoints, 256]

        // After processing, back to CPU only descriptor
        if (use_cuda)
            desc = desc.to(kCPU);

        cv::Mat desc_no_nms(cv::Size(desc.size(1), desc.size(0)), CV_32FC1, desc.data_ptr<float>());

        // From torch::Tensor   kpts(=keypoints)
        // To   cv::KeyPoint    keypoints_no_nms
        std::vector<cv::KeyPoint> keypoints_no_nms;
        for (int i = 0; i < kpts.size(0); i++)
        {
            float response = prob[kpts[i][0]][kpts[i][1]].item<float>();
            keypoints_no_nms.push_back(cv::KeyPoint(kpts[i][1].item<float>(), kpts[i][0].item<float>(), 8, -1, response));
        }

        cv::Mat kpt_mat(keypoints_no_nms.size(), 2, CV_32F);
        cv::Mat conf(keypoints_no_nms.size(), 1, CV_32F);
        for (size_t i = 0; i < keypoints_no_nms.size(); i++)
        {
            int x = keypoints_no_nms[i].pt.x;
            int y = keypoints_no_nms[i].pt.y;
            kpt_mat.at<float>(i, 0) = (float)keypoints_no_nms[i].pt.x;
            kpt_mat.at<float>(i, 1) = (float)keypoints_no_nms[i].pt.y;

            conf.at<float>(i, 0) = prob[y][x].item<float>();
        }

        cv::Mat descriptors;
        std::vector<cv::KeyPoint> keypoints;

        int border = 8;
        int dist_thresh = 4;
        int height = img.rows;
        int width = img.cols;

        NMS(kpt_mat, conf, desc_no_nms, keypoints, descriptors, border, dist_thresh, width, height);

        return descriptors;
    }

    void SuperPointFrontend::getKeyPoints(float threshold, int iniX, int maxX, int iniY, int maxY, std::vector<cv::KeyPoint> &keypoints, bool nms)
    {
        auto prob = mProb.slice(0, iniY, maxY).slice(1, iniX, maxX); // [h, w]
        auto kpts = (prob > threshold);
        kpts = nonzero(kpts); // [n_keypoints, 2]  (y, x)

        std::vector<cv::KeyPoint> keypoints_no_nms;
        for (int i = 0; i < kpts.size(0); i++)
        {
            float response = prob[kpts[i][0]][kpts[i][1]].item<float>();
            keypoints_no_nms.push_back(cv::KeyPoint(kpts[i][1].item<float>(), kpts[i][0].item<float>(), 8, -1, response));
        }

        if (nms)
        {
            cv::Mat conf(keypoints_no_nms.size(), 1, CV_32F);
            std::cout << "SPDetector::getKeyPoints : conf's size: " << conf.size() << std::endl;

            for (size_t i = 0; i < keypoints_no_nms.size(); i++)
            {
                int x = keypoints_no_nms[i].pt.x;
                int y = keypoints_no_nms[i].pt.y;
                conf.at<float>(i, 0) = prob[y][x].item<float>();
            }

            // cv::Mat descriptors;

            int border = 0;
            int dist_thresh = 4;
            int height = maxY - iniY;
            int width = maxX - iniX;

            // Keypoints의 좌표를 담은 [n, 2] 벡터와 Confidence를 담은 [n, 1] 벡터.
            NMS2(keypoints_no_nms, conf, keypoints, border, dist_thresh, width, height);
        }
        else
        {
            keypoints = keypoints_no_nms;
        }
    }

    void SuperPointFrontend::computeDescriptors(const std::vector<cv::KeyPoint> &keypoints, cv::Mat &descriptors)
    {
        cv::Mat kpt_mat(keypoints.size(), 2, CV_32F); // [n_keypoints, 2]  (y, x)

        for (size_t i = 0; i < keypoints.size(); i++)
        {
            kpt_mat.at<float>(i, 0) = (float)keypoints[i].pt.y;
            kpt_mat.at<float>(i, 1) = (float)keypoints[i].pt.x;
        }

        auto fkpts = torch::from_blob(kpt_mat.data, {(long long)keypoints.size(), 2}, kFloat); //CHANGED (long long)

        auto grid = torch::zeros({1, 1, fkpts.size(0), 2});                         // [1, 1, n_keypoints, 2]
        grid[0][0].slice(1, 0, 1) = 2.0 * fkpts.slice(1, 1, 2) / mProb.size(1) - 1; // x
        grid[0][0].slice(1, 1, 2) = 2.0 * fkpts.slice(1, 0, 1) / mProb.size(0) - 1; // y

        auto desc = grid_sampler(mDesc, grid, 0, 0, false); // [1, 256, 1, n_keypoints]         //CHANGED ,false
        desc = desc.squeeze(0).squeeze(1);                  // [256, n_keypoints]

        // normalize to 1
        auto dn = norm(desc, 2, 1);
        desc = desc.div(unsqueeze(dn, 1));

        desc = desc.transpose(0, 1).contiguous(); // [n_keypoints, 256]
        desc = desc.to(kCPU);

        cv::Mat desc_mat(cv::Size(desc.size(1), desc.size(0)), CV_32FC1, desc.data_ptr<float>());

        descriptors = desc_mat.clone();
    }

    void SuperPointFrontend::NMS2(std::vector<cv::KeyPoint> det, cv::Mat conf, std::vector<cv::KeyPoint> &pts,
                                  int border, int dist_thresh, int img_width, int img_height)
    {

        std::vector<cv::Point2f> pts_raw;

        for (int i = 0; i < det.size(); i++)
        {

            int u = (int)det[i].pt.x;
            int v = (int)det[i].pt.y;

            pts_raw.push_back(cv::Point2f(u, v));
        }

        cv::Mat grid = cv::Mat(cv::Size(img_width, img_height), CV_8UC1);
        cv::Mat inds = cv::Mat(cv::Size(img_width, img_height), CV_16UC1);

        cv::Mat confidence = cv::Mat(cv::Size(img_width, img_height), CV_32FC1);

        grid.setTo(0);
        inds.setTo(0);
        confidence.setTo(0);

        for (int i = 0; i < pts_raw.size(); i++)
        {
            int uu = (int)pts_raw[i].x;
            int vv = (int)pts_raw[i].y;

            grid.at<char>(vv, uu) = 1;
            inds.at<unsigned short>(vv, uu) = i;

            confidence.at<float>(vv, uu) = conf.at<float>(i, 0);
        }

        cv::copyMakeBorder(grid, grid, dist_thresh, dist_thresh, dist_thresh, dist_thresh, cv::BORDER_CONSTANT, 0);

        for (int i = 0; i < pts_raw.size(); i++)
        {
            int uu = (int)pts_raw[i].x + dist_thresh;
            int vv = (int)pts_raw[i].y + dist_thresh;

            if (grid.at<char>(vv, uu) != 1)
                continue;

            for (int k = -dist_thresh; k < (dist_thresh + 1); k++)
                for (int j = -dist_thresh; j < (dist_thresh + 1); j++)
                {
                    if (j == 0 && k == 0)
                        continue;

                    if (confidence.at<float>(vv + k, uu + j) < confidence.at<float>(vv, uu))
                        grid.at<char>(vv + k, uu + j) = 0;
                }
            grid.at<char>(vv, uu) = 2;
        }

        size_t valid_cnt = 0;
        std::vector<int> select_indice;

        for (int v = 0; v < (img_height + dist_thresh); v++)
        {
            for (int u = 0; u < (img_width + dist_thresh); u++)
            {
                if (u - dist_thresh >= (img_width - border) || u - dist_thresh < border || v - dist_thresh >= (img_height - border) || v - dist_thresh < border)
                    continue;

                if (grid.at<char>(v, u) == 2)
                {
                    int select_ind = (int)inds.at<unsigned short>(v - dist_thresh, u - dist_thresh);
                    cv::Point2f p = pts_raw[select_ind];
                    float response = conf.at<float>(select_ind, 0);
                    pts.push_back(cv::KeyPoint(p, 8.0f, -1, response));

                    select_indice.push_back(select_ind);
                    valid_cnt++;
                }
            }
        }

        // descriptors.create(select_indice.size(), 256, CV_32F);

        // for (int i=0; i<select_indice.size(); i++)
        // {
        //     for (int j=0; j < 256; j++)
        //     {
        //         descriptors.at<float>(i, j) = desc.at<float>(select_indice[i], j);
        //     }
        // }
    }

    void SuperPointFrontend::NMS(cv::Mat det, cv::Mat conf, cv::Mat desc, std::vector<cv::KeyPoint> &pts, cv::Mat &descriptors,
                                 int border, int dist_thresh, int img_width, int img_height)
    {

        std::vector<cv::Point2f> pts_raw;

        for (int i = 0; i < det.rows; i++)
        {

            int u = (int)det.at<float>(i, 0);
            int v = (int)det.at<float>(i, 1);
            // float conf = det.at<float>(i, 2);

            pts_raw.push_back(cv::Point2f(u, v));
        }

        cv::Mat grid = cv::Mat(cv::Size(img_width, img_height), CV_8UC1);
        cv::Mat inds = cv::Mat(cv::Size(img_width, img_height), CV_16UC1);

        cv::Mat confidence = cv::Mat(cv::Size(img_width, img_height), CV_32FC1);

        grid.setTo(0);
        inds.setTo(0);
        confidence.setTo(0);

        for (int i = 0; i < pts_raw.size(); i++)
        {
            int uu = (int)pts_raw[i].x;
            int vv = (int)pts_raw[i].y;

            grid.at<char>(vv, uu) = 1;
            inds.at<unsigned short>(vv, uu) = i;

            confidence.at<float>(vv, uu) = conf.at<float>(i, 0);
        }

        cv::copyMakeBorder(grid, grid, dist_thresh, dist_thresh, dist_thresh, dist_thresh, cv::BORDER_CONSTANT, 0);

        for (int i = 0; i < pts_raw.size(); i++)
        {
            int uu = (int)pts_raw[i].x + dist_thresh;
            int vv = (int)pts_raw[i].y + dist_thresh;

            if (grid.at<char>(vv, uu) != 1)
                continue;

            for (int k = -dist_thresh; k < (dist_thresh + 1); k++)
                for (int j = -dist_thresh; j < (dist_thresh + 1); j++)
                {
                    if (j == 0 && k == 0)
                        continue;

                    if (conf.at<float>(vv + k, uu + j) < conf.at<float>(vv, uu))
                        grid.at<char>(vv + k, uu + j) = 0;
                }
            grid.at<char>(vv, uu) = 2;
        }

        size_t valid_cnt = 0;
        std::vector<int> select_indice;

        for (int v = 0; v < (img_height + dist_thresh); v++)
        {
            for (int u = 0; u < (img_width + dist_thresh); u++)
            {
                if (u - dist_thresh >= (img_width - border) || u - dist_thresh < border || v - dist_thresh >= (img_height - border) || v - dist_thresh < border)
                    continue;

                if (grid.at<char>(v, u) == 2)
                {
                    int select_ind = (int)inds.at<unsigned short>(v - dist_thresh, u - dist_thresh);
                    pts.push_back(cv::KeyPoint(pts_raw[select_ind], 1.0f));

                    select_indice.push_back(select_ind);
                    valid_cnt++;
                }
            }
        }

        descriptors.create(select_indice.size(), 256, CV_32F);

        for (int i = 0; i < select_indice.size(); i++)
        {
            for (int j = 0; j < 256; j++)
            {
                descriptors.at<float>(i, j) = desc.at<float>(select_indice[i], j);
            }
        }
    }

    void printSection(int n, std::string s)
    {
        std::cout << "\n < TEST Section " << n << " > \n"
                  << "--Test about " << s << "--\n\n";
    }

} // namespace NAMU_TEST