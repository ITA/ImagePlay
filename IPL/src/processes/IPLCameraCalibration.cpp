//#############################################################################
//
//  This file is part of ImagePlay.
//
//  ImagePlay is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  ImagePlay is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with ImagePlay.  If not, see <http://www.gnu.org/licenses/>.
//
//#############################################################################

#include "IPLCameraCalibration.h"

void IPLCameraCalibration::init()
{
    // init
    _preview    = NULL;
    _mode       = DETECTION;
    _frameCounter = 0;

    // basic settings
    setClassName("IPLCameraCalibration");
    setTitle("Camera Calibration");
    setCategory(IPLProcess::CATEGORY_OBJECTS);
    setOpenCVSupport(IPLOpenCVSupport::OPENCV_ONLY);

    // inputs and outputs
    addInput("Image", IPL_IMAGE_COLOR);
    addOutput("Preview", IPL_IMAGE_COLOR);

    // properties
    addProcessPropertyString("fileName", "File Name:xml", "Save and load XML files", "", IPL_WIDGET_FILE_SAVE);
    addProcessPropertyInt("saveCalibration", "Save Calibration", "", false, IPL_WIDGET_BUTTON);
    addProcessPropertyInt("loadCalibration", "Load Calibration", "", false, IPL_WIDGET_BUTTON);

    addProcessPropertyInt("targetType", "Target:CHESSBOARD|CIRCLES_GRID|ASYMMETRIC_CIRCLES_GRID", "", 0, IPL_WIDGET_COMBOBOX);
    addProcessPropertyInt("targetCols", "Target Columns", "", 4, IPL_WIDGET_SLIDER, 3, 20);
    addProcessPropertyInt("targetRows", "Target Rows", "", 7, IPL_WIDGET_SLIDER, 3, 20);
    addProcessPropertyUnsignedInt("skipFrames", "Skip Frames", "", 10, IPL_WIDGET_SLIDER, 1, 100);
}

void IPLCameraCalibration::destroy()
{
    delete _preview;

}

void IPLCameraCalibration::processPropertyEvents(IPLEvent* e)
{
    resetMessages();

    if(e->name() == "saveCalibration")
    {
        std::string fileName = getProcessPropertyString("fileName");
    }
    if(e->name() == "loadCalibration")
    {
        std::string fileName = getProcessPropertyString("fileName");
    }
    addInformation(e->name());
}

bool IPLCameraCalibration::processInputData(IPLData* data , int index, bool useOpenCV)
{
    _image = data->toImage();

    // get properties
    std::string fileName    = getProcessPropertyString("fileName");
    int targetType          = getProcessPropertyInt("targetType");
    int targetCols          = getProcessPropertyInt("targetCols");
    int targetRows          = getProcessPropertyInt("targetRows");
    int skipFrames          = getProcessPropertyUnsignedInt("skipFrames");

    std::stringstream s3;
    s3 << "MODE: " << _mode;
    addSuccess(s3.str());



    notifyProgressEventHandler(-1);
    cv::Mat input;
    cv::Mat output = _image->toCvMat();
    cv::cvtColor(_image->toCvMat(), input, cv::COLOR_BGR2GRAY);

    std::vector<cv::Point2f>              pointBuf;
    cv::Size                              boardSize(targetCols, targetRows);
    cv::Size                              imageSize(_image->width(), _image->height());
    std::vector<std::vector<cv::Point3f>> objectPoints(1);
    cv::Mat                               cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat                               distCoeffs = cv::Mat::zeros(8, 1, CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;
    std::vector<float> reprojErrs;
    double totalAvgErr = 0;


    if(_mode == CALIBRATED)
    {
        std::stringstream s2;
        s2 << "totalAvgErr: ";
        s2 << totalAvgErr;
        addInformation(s2.str());

        addSuccess("Calibration successful.");
    }

    // skip a few frames when using a camera
    if(_frameCounter < skipFrames)
    {
        delete _preview;
        _preview = new IPLImage(*_image);
        _frameCounter++;
        return true;
    }
    _frameCounter = 0;

    if(_mode == DETECTION)
    {
        bool found;
        switch(targetType) // Find feature points on the input format
        {
            case 0: // CHESSBOARD
                found = cv::findChessboardCorners(input, boardSize, pointBuf, cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK | cv::CALIB_CB_NORMALIZE_IMAGE);
            break;
            case 1: //CIRCLES_GRID:
                found = cv::findCirclesGrid(input, boardSize, pointBuf);
            break;
            case 2: //ASYMMETRIC_CIRCLES_GRID:
                found = cv::findCirclesGrid(input, boardSize, pointBuf, cv::CALIB_CB_ASYMMETRIC_GRID);
            break;
        }

        // computing descriptors
        if (found)
        {
            // improve the found corners' coordinate accuracy for chessboard
            if(targetType == 0) // CHESSBOARD
            {
                cv::cornerSubPix(input, pointBuf, cv::Size(11,11), cv::Size(-1,-1), cv::TermCriteria( cv::TermCriteria::EPS+cv::TermCriteria::MAX_ITER, 30, 0.1 ));
            }

            _imagePoints.push_back(pointBuf);
        }
        drawChessboardCorners(output, boardSize, cv::Mat(pointBuf), found);

        std::stringstream s1;
        s1 << "Number of good images: ";
        s1 << _imagePoints.size();
        addInformation(s1.str());

        if(_imagePoints.size() > 10)
        {
            _mode = CALIBRATION;
        }
    }

    if(_mode == CALIBRATION)
    {
        Pattern patternType = (Pattern) targetType;
        bool result = this->runCalibration(_imagePoints, imageSize, boardSize, patternType, 1, 1, cv::CALIB_FIX_K4 | cv::CALIB_FIX_K5, cameraMatrix,
                                distCoeffs, rvecs, tvecs, reprojErrs, totalAvgErr);

        if(result) {
            _mode = CALIBRATED;

        } else {
            addError("Unable to calibrate.");
        }
    }

    delete _preview;
    _preview = new IPLImage(output);

    return true;
}

IPLData* IPLCameraCalibration::getResultData( int index )
{
    return _preview;
}

bool IPLCameraCalibration::runCalibration(std::vector<std::vector<cv::Point2f> > imagePoints,
                    cv::Size imageSize, cv::Size boardSize, Pattern patternType,
                    float squareSize, float aspectRatio,
                    int flags, cv::Mat& cameraMatrix, cv::Mat& distCoeffs,
                    std::vector<cv::Mat>& rvecs, std::vector<cv::Mat>& tvecs,
                    std::vector<float>& reprojErrs,
                    double& totalAvgErr)
{
    cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    if( flags & cv::CALIB_FIX_ASPECT_RATIO )
        cameraMatrix.at<double>(0,0) = aspectRatio;

    distCoeffs = cv::Mat::zeros(8, 1, CV_64F);

    std::vector<std::vector<cv::Point3f> > objectPoints(1);
    this->calcChessboardCorners(boardSize, squareSize, objectPoints[0], patternType);

    objectPoints.resize(imagePoints.size(),objectPoints[0]);

    double rms = calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix,
                    distCoeffs, rvecs, tvecs, flags|cv::CALIB_FIX_K4|cv::CALIB_FIX_K5);
                    ///*|CALIB_FIX_K3*/|CALIB_FIX_K4|CALIB_FIX_K5);

    bool ok = checkRange(cameraMatrix) && checkRange(distCoeffs);

    totalAvgErr = this->computeReprojectionErrors(objectPoints, imagePoints,
                rvecs, tvecs, cameraMatrix, distCoeffs, reprojErrs);

    return ok;
}

void IPLCameraCalibration::calcChessboardCorners(cv::Size boardSize, float squareSize, std::vector<cv::Point3f>& corners, Pattern patternType)
{
    corners.clear();

    switch(patternType)
    {
    case CHESSBOARD:
    case CIRCLES_GRID:
      for( int i = 0; i < boardSize.height; ++i )
        for( int j = 0; j < boardSize.width; ++j )
            corners.push_back(cv::Point3f(float( j*squareSize ), float( i*squareSize ), 0));
      break;

    case ASYMMETRIC_CIRCLES_GRID:
      for( int i = 0; i < boardSize.height; i++ )
         for( int j = 0; j < boardSize.width; j++ )
            corners.push_back(cv::Point3f(float((2*j + i % 2)*squareSize), float(i*squareSize), 0));
      break;
    }
}

double IPLCameraCalibration::computeReprojectionErrors(
        const std::vector<std::vector<cv::Point3f> >& objectPoints,
        const std::vector<std::vector<cv::Point2f> >& imagePoints,
        const std::vector<cv::Mat>& rvecs, const std::vector<cv::Mat>& tvecs,
        const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
        std::vector<float>& perViewErrors )
{
    std::vector<cv::Point2f> imagePoints2;
    int i, totalPoints = 0;
    double totalErr = 0, err;
    perViewErrors.resize(objectPoints.size());

    for( i = 0; i < (int)objectPoints.size(); i++ )
    {
        projectPoints(cv::Mat(objectPoints[i]), rvecs[i], tvecs[i],
                      cameraMatrix, distCoeffs, imagePoints2);
        err = cv::norm(cv::Mat(imagePoints[i]), cv::Mat(imagePoints2), cv::NORM_L2);
        int n = (int)objectPoints[i].size();
        perViewErrors[i] = (float)std::sqrt(err*err/n);
        totalErr += err*err;
        totalPoints += n;
    }

    return std::sqrt(totalErr/totalPoints);
}
/*
static void IPLCameraCalibration::saveCameraParams( const string& filename,
                       Size imageSize, Size boardSize,
                       float squareSize, float aspectRatio, int flags,
                       const Mat& cameraMatrix, const Mat& distCoeffs,
                       const vector<Mat>& rvecs, const vector<Mat>& tvecs,
                       const vector<float>& reprojErrs,
                       const vector<vector<Point2f> >& imagePoints,
                       double totalAvgErr )
{
    cv::FileStorage fs( filename, cv::FileStorage::WRITE );

    time_t tt;
    time( &tt );
    struct tm *t2 = localtime( &tt );
    char buf[1024];
    strftime( buf, sizeof(buf)-1, "%c", t2 );

    fs << "calibration_time" << buf;

    if( !rvecs.empty() || !reprojErrs.empty() )
        fs << "nframes" << (int)std::max(rvecs.size(), reprojErrs.size());
    fs << "image_width" << imageSize.width;
    fs << "image_height" << imageSize.height;
    fs << "board_width" << boardSize.width;
    fs << "board_height" << boardSize.height;
    fs << "square_size" << squareSize;

    if( flags & CALIB_FIX_ASPECT_RATIO )
        fs << "aspectRatio" << aspectRatio;

    if( flags != 0 )
    {
        sprintf( buf, "flags: %s%s%s%s",
            flags & CALIB_USE_INTRINSIC_GUESS ? "+use_intrinsic_guess" : "",
            flags & CALIB_FIX_ASPECT_RATIO ? "+fix_aspectRatio" : "",
            flags & CALIB_FIX_PRINCIPAL_POINT ? "+fix_principal_point" : "",
            flags & CALIB_ZERO_TANGENT_DIST ? "+zero_tangent_dist" : "" );
        //cvWriteComment( *fs, buf, 0 );
    }

    fs << "flags" << flags;

    fs << "camera_matrix" << cameraMatrix;
    fs << "distortion_coefficients" << distCoeffs;

    fs << "avg_reprojection_error" << totalAvgErr;
    if( !reprojErrs.empty() )
        fs << "per_view_reprojection_errors" << Mat(reprojErrs);

    if( !rvecs.empty() && !tvecs.empty() )
    {
        CV_Assert(rvecs[0].type() == tvecs[0].type());
        Mat bigmat((int)rvecs.size(), 6, rvecs[0].type());
        for( int i = 0; i < (int)rvecs.size(); i++ )
        {
            Mat r = bigmat(Range(i, i+1), Range(0,3));
            Mat t = bigmat(Range(i, i+1), Range(3,6));

            CV_Assert(rvecs[i].rows == 3 && rvecs[i].cols == 1);
            CV_Assert(tvecs[i].rows == 3 && tvecs[i].cols == 1);
            //*.t() is MatExpr (not Mat) so we can use assignment operator
            r = rvecs[i].t();
            t = tvecs[i].t();
        }
        //cvWriteComment( *fs, "a set of 6-tuples (rotation vector + translation vector) for each view", 0 );
        fs << "extrinsic_parameters" << bigmat;
    }

    if( !imagePoints.empty() )
    {
        Mat imagePtMat((int)imagePoints.size(), (int)imagePoints[0].size(), CV_32FC2);
        for( int i = 0; i < (int)imagePoints.size(); i++ )
        {
            Mat r = imagePtMat.row(i).reshape(2, imagePtMat.cols);
            Mat imgpti(imagePoints[i]);
            imgpti.copyTo(r);
        }
        fs << "image_points" << imagePtMat;
    }
}*/
