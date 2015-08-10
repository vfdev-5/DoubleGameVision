
// Std
#include <iostream>
#include <vector>

// Qt
#include <QString>
#include <QDir>
#include <QFile>

// Opencv
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/shape.hpp>
#include <opencv2/features2d.hpp>

// Project
#include "CardDetector.h"
#include "Core/Global.h"
#include "Core/ImageCommon.h"
#include "Core/ImageFiltering.h"


bool VERBOSE = false;

/*!
 * OK - Find circles and mask their content = Find cards
 * OK - Rectify circles and its content = Rectify card geometry
 * OK - Extract objects from each circle = Extract objects from each card
 *  -- Features : Hu moments
 *  -- Or directly use 'Match Shapes' function
 * - Compare objects
*/

void help()
{
    SD_TRACE("Usage : DGVApp image_data_path");
    SD_TRACE("  where image_data_path is a path with *.jpg, *.png, *.tif images");
    SD_TRACE("Example : DGVApp C:/Temp/");
}

int main(int argc, char** argv)
{

    if (argc != 2)
    {
        help();
        return 0;
    }

    // ----- LOAD IMAGES FROM PATH
    QString path = QString(argv[1]);
    QDir d(path);
    if (!d.exists())
    {
        SD_TRACE1("Provided path '%1' is not found", path);
        return 1;
    }

    QStringList files = d.entryList(QStringList() << "*.jpg" << "*.png" << "*.tif", QDir::Files);
    if (files.isEmpty())
    {
        SD_TRACE1("No images found at path '%1'", path);
        help();
        return 1;
    }

#if 0
    QStringList filesToOpen = files;
#else
    QStringList filesToOpen = QStringList() << files[3];
#endif

    int cardSizeMin = 100;
    int cardSizeMax = 400;
    DGV::CardDetector cardDetector(cardSizeMin, cardSizeMax, VERBOSE);

    // Loop on files :
    foreach (QString file, filesToOpen)
    {
        SD_TRACE1("Open file '%1'", file);
        QString f = path + "/" + file;
        cv::Mat inImage = cv::imread(f.toStdString(), cv::IMREAD_GRAYSCALE);

        ImageCommon::displayMat(inImage, true, "Input image");

        // Resize image
        cv::Mat procImage = inImage;
        int dim = qMax(procImage.rows, procImage.cols);
        int limit = 700;
        if (dim > limit)
        {
            cv::Mat out;
            double f = limit* 1.0 / dim;
            cv::resize(procImage, out, cv::Size(), f, f);
            procImage = out;
        }

        cv::Mat i0;
        procImage.copyTo(i0);

        // ---- FIND CARDS
        QVector<cv::Mat> cards = cardDetector.detectCards(procImage);
        if (cards.isEmpty())
        {
            SD_ERR("No cards found");
            return 0;
        }

        // ---- UNIFY SIZE OF THE CARDS
        int uniDim = (cardSizeMin + cardSizeMax)/2;


        SD_TRACE(QString("Uniform size : %1, %2").arg(uniDim).arg(uniDim));
        QVector<cv::Mat> uniCards = cardDetector.uniformSize(cards, uniDim);

        // ---- EXTRACT OBJECTS AND MATCH SHAPES BETWEEN TWO CARD

//        cv::Ptr< cv::HausdorffDistanceExtractor > matcher = cv::createHausdorffDistanceExtractor();
//        int WTA_K = 3;
//        cv::Ptr<cv::Feature2D> extractor = cv::ORB::create(100, 1.2, 8, 31, 0, WTA_K);
#if 1
        cv::Ptr<cv::Feature2D> extractor = cv::AKAZE::create(cv::AKAZE::DESCRIPTOR_KAZE, 1);
//        cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create("BruteForce-Hamming");
        cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create("FlannBased");
//        int goodDistance = 35;
        float goodDistance = 0.30;
#else
        cv::Ptr<cv::Feature2D> extractor = cv::KAZE::create();
        cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create("FlannBased");
        float goodDistance = 0.25;
#endif
        int goodMatchesMinLimit = 10;

//        VERBOSE = true;
        while (!uniCards.isEmpty())
        {

            // TAKE ONE CARD
            cv::Mat card = uniCards.takeFirst();
            if (uniCards.isEmpty()) break;

//            ImageCommon::displayMat(card, true, QString("Card"));
            QVector<std::vector<cv::Point> > oContours1;
            cardDetector.extractObjects(card, &oContours1);
            ImageCommon::displayContour(oContours1.toStdVector(), card, false, true, "Card");

            // TAKE ANOTHER CARD
            foreach (cv::Mat anotherCard, uniCards)
            {


//                ImageCommon::displayMat(anotherCard, true, QString("Another card"));
                QVector<std::vector<cv::Point> > oContours2;
                cardDetector.extractObjects(anotherCard, &oContours2);
                ImageCommon::displayContour(oContours2.toStdVector(), anotherCard, false, true, "Another card");

                StartTimer("Compare two cards");

                // LOOP ON THE OBJECTS FROM THE CARD ONE:
                for (int i=0;i<oContours1.size();i++)
                {

                    std::vector<cv::KeyPoint> keypoints_1;
                    cv::Mat descriptors_1;
                    cv::Mat object_1 = cardDetector.getObject(card, oContours1[i]);
//                    cv::Mat object_1 = card;
//                    ImageCommon::displayMat(object_1, true, "Object 1");
                    extractor->detectAndCompute(object_1,
                                                cv::Mat(),
                                                keypoints_1,
                                                descriptors_1);
//                    SD_TRACE1("Nb of keypoints (card) : %1", keypoints_1.size());
//                    ImageCommon::displayMat(descriptors_1, true, "Descriptors 1");
                    matcher->clear();
                    matcher->add(descriptors_1);
                    matcher->train();

//                    double rmin = 1e10;
//                    int index=-1;
                    bool matchFound=false;

                    // LOOP ON THE OBJECTS OF THE CARD TWO
                    for (int j=0;j<oContours2.size();j++)
                    {

                        std::vector<cv::KeyPoint> keypoints_2;
                        cv::Mat descriptors_2;
                        cv::Mat object_2 = cardDetector.getObject(anotherCard, oContours2[j]);
//                        ImageCommon::displayMat(object_2, true, "Object 2");
                        extractor->detectAndCompute(object_2,
                                                    cv::Mat(),
                                                    keypoints_2,
                                                    descriptors_2);
//                        SD_TRACE1("Nb of keypoints (anotherCard) : %1", keypoints_2.size());
//                        ImageCommon::displayMat(descriptors_2, true, "Descriptors 2");

                        std::vector<cv::DMatch> matchedKeypoints;

                        matcher->match(descriptors_2, matchedKeypoints);

//                        SD_TRACE1(" Matched keypoints count : %1", matchedKeypoints.size());
                        // Sort matches
                        std::sort(matchedKeypoints.begin(), matchedKeypoints.end());

                        if (VERBOSE) SD_TRACE2("Matches : min/max distances : %1, %2", matchedKeypoints[0].distance, matchedKeypoints[matchedKeypoints.size()-1].distance);
//                        SD_TRACE2("Matches : min/max distances : %1, %2", matchedKeypoints[0].distance, matchedKeypoints[matchedKeypoints.size()-1].distance);

                        // Select "good" matches
                        std::vector<cv::DMatch> goodMatches;
                        for (int i=0; i<matchedKeypoints.size();i++)
                        {
                            cv::DMatch m = matchedKeypoints[i];
                            if (m.distance < goodDistance)
                            {
                                goodMatches.push_back(m);
                            }
                        }
                        if (VERBOSE) SD_TRACE1("Good matched keypoints count : %1", goodMatches.size());
//                        SD_TRACE1("Good matched keypoints count : %1", goodMatches.size());

                        if (VERBOSE) {
                            // DISPLAY
                            cv::Mat out;
                            std::vector<std::vector<cv::DMatch> > vectorOfMatchedKeypoints;
                            vectorOfMatchedKeypoints.push_back(goodMatches);
                            cv::drawMatches(object_1, keypoints_1, object_2, keypoints_2, vectorOfMatchedKeypoints, out);
                            ImageCommon::displayMat(out, true, "Matched keypoints");
                        }

                        if (goodMatches.size() >= goodMatchesMinLimit)
                        {
                            matchFound = true;

                            SD_TRACE2("Match found between object %1 on the 1st card and object %2 on the second card", i, j);

                            // DISPLAY THE RESULT :
                            cv::Mat out;
                            std::vector<std::vector<cv::DMatch> > vectorOfMatchedKeypoints;
                            vectorOfMatchedKeypoints.push_back(goodMatches);
                            cv::drawMatches(object_1, keypoints_1, object_2, keypoints_2, vectorOfMatchedKeypoints, out);
                            ImageCommon::displayMat(out, true, "Matched keypoints");

                            break;
                        }


//                        double r = cv::matchShapes(oContours1[i], oContours2[j], CV_CONTOURS_MATCH_I1, 0.0);
//                        double r = matcher->computeDistance(oContours1[i], oContours2[j]);
//                        if (r < rmin)
//                        {
//                            rmin = r;
//                            index = j;
//                        }
//                        SD_TRACE3("Match shapes : %1, %2, %3", i, j, r);

                    }

//                    if (index >= 0)
//                    {
//                        // Show result :
//                        SD_TRACE3("Show result : Contour %1 with matched contour %2 with ratio = %3", i, index, rmin);
//                        std::vector<std::vector<cv::Point> > c1, c2;
//                        c1.push_back(oContours1[i]);
//                        c2.push_back(oContours2[index]);
//                        ImageCommon::displayContour(c1, card, false, true, "Card");
//                        ImageCommon::displayContour(c2, anotherCard, false, true, "Another card");
//                    }
//                    else
//                    {
//                        // No matches
//                        SD_TRACE("NO MATCHES FOUND");
//                    }


                    // IF MATCH IS FOUND -> NO NEED TO COMPARE THESE CARDS
                    if (matchFound)
                    {
                        break;
                    }

                }


                StopTimer();

            }
        }


        return 0;
    }

}

// *******************************************************
// ***********
// *********** TESTS TO REMOVE ***************************
// ***********
// *******************************************************


#if 0
    // IMAGE SIMULATION
    cv::Mat in(450, 450, CV_8U, cv::Scalar::all(0));

    {

        cv::Mat c1 = ImageFiltering::getCircleKernel2D(cv::Size(40, 40));
        cv::Mat c2 = ImageFiltering::getCircleKernel2D(cv::Size(50, 40));
        cv::Mat c3 = ImageFiltering::getCircleKernel2D(cv::Size(60, 70));
        cv::Mat c4 = ImageFiltering::getCircleKernel2D(cv::Size(55, 55));

        c1.copyTo(in(cv::Rect(50, 60, 40, 40)));
        c2.copyTo(in(cv::Rect(100, 20, 50, 40)));
        c3.copyTo(in(cv::Rect(20, 100, 60, 70)));
        c4.copyTo(in(cv::Rect(100,100,55,55)));

        cv::Mat noise(in.rows, in.cols, in.type());
        cv::Mat noise2(in.rows, in.cols, in.type());
        cv::randn(noise, 150, 15);
        cv::randn(noise2, 122, 20);

        in = in - noise + noise2;
    }
    ImageCommon::displayMat(in, true, "Circle");


    cv::Mat mask = ImageFiltering::detectCircles(in, 55);

//    cv::Laplacian(in, in, CV_8U);
//    ImageCommon::displayMat(in, true, "Laplacian");

//    cv::blur(in, in, cv::Size(5,5));
//    ImageCommon::displayMat(in, true, "Blur");

//    cv::Mat t1, t2, out;
//    cv::Mat templ = ImageFiltering::getCircleKernel2D(cv::Size(55, 55));
//    cv::matchTemplate(in,templ,t1,cv::TM_CCOEFF_NORMED);
//    ImageCommon::displayMat(t1, true, "Corr");
//    cv::threshold(t1, t2, 0.5, 255, CV_THRESH_BINARY);

//    t2.convertTo(out, CV_8U);
////    out = cv::Mat(in.rows, in.cols, in.type(), cv::Scalar::all(0));
////    cv::Rect r(templ.cols/2, templ.rows/2, t1.cols, t1.rows);
////    t2.copyTo(out(r));

//    ImageCommon::displayMat(out, true, "Corr");

//    std::vector< std::vector<cv::Point> > contours;
//    cv::Point offset(templ.cols/2, templ.rows/2);
//    cv::findContours(out, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE, offset);

//    cv::Mat mask = cv::Mat(in.rows, in.cols, in.type(), cv::Scalar::all(0));

//    for (uint i=0; i<contours.size();i++)
//    {
//        std::vector<cv::Point> contour = contours[i];
//        cv::Rect brect = cv::boundingRect(contour);
//        cv::Rect r(brect.tl().x, brect.tl().y, templ.cols, templ.rows);
//        cv::Mat m = templ/255 * (i+1);
//        m.copyTo(mask(r));
//    }

    ImageCommon::displayMat(mask, true, "mask");

    ImageCommon::displayMat(in.mul(mask), true, "Circle detection");
    return 0;
#endif

#if 0
    // Sharpen
    cv::Mat t1, t2;

    cv::Laplacian(procImage, t1, CV_8U,3);
    ImageCommon::displayMat(t1, true, "Laplacian");


    int size = 5*80;
    double sigma = size*0.25;
    //    cv::Mat freqMask = cv::Mat::ones(240, 240, CV_32F);
    cv::Mat freqMask = ImageFiltering::getGaussianKernel2D(cv::Size(size, size), sigma, sigma);
    ImageFiltering::freqFilter(t1, t2, freqMask);
    ImageCommon::displayMat(t2, true, "Low freq image");

    //    int extSize = 2*80;
    //    double extSigma = extSize*0.25;
    //    cv::Mat freqMaskExt = ImageFiltering::getGaussianKernel2D(cv::Size(extSize, extSize), extSigma, extSigma);
    //    cv::pow(freqMaskExt,2.0,freqMaskExt);
    //    ImageFiltering::freqFilter(t1, t2, freqMaskExt, false);
    //    ImageCommon::displayMat(t2, true, "High freq image");

    procImage = t2 - 0.5*t1;
    ImageCommon::displayMat(procImage, true, "Image sharpening");

    double minVal, maxVal;
    cv::minMaxLoc(procImage, &minVal,&maxVal);
    //    double tVal = minVal + 0.1 * (maxVal-minVal);
    //    cv::threshold(procImage, procImage, tVal, 255, CV_THRESH_BINARY_INV);
    //    ImageCommon::displayMat(procImage, true, "Thresholded");

    //    cv::Mat t3;
    //    procImage.convertTo(t3, CV_32F, 1.0/(maxVal-minVal), -minVal/(maxVal-minVal));
    cv::absdiff(maxVal, procImage, procImage);
    ImageCommon::displayMat(procImage, true, "Inverted Image sharpening");

#endif

#if 0

    // Find cards = find circles and mask their content
    // Smooth
    cv::blur(procImage, procImage, cv::Size(3,3));
    ImageCommon::displayMat(procImage, true, "Blurred");

    std::vector<cv::Vec3f> circles;
    int minDist = cardSizeMin;
    cv::HoughCircles(procImage, circles, cv::HOUGH_GRADIENT,
                     1, minDist, 150, 70, cardSizeMin/2, cardSizeMax);

    // display result :
    cv::Mat img;
    procImage.copyTo(img);
    for( size_t i = 0; i < circles.size(); i++ )
    {
        cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
        int radius = cvRound(circles[i][2]);
        // draw the circle center
        cv::circle( img, center, 3, cv::Scalar(0,255,0), -1, 8, 0 );
        // draw the circle outline
        cv::circle( img, center, radius, cv::Scalar(0,0,255), 3, 8, 0 );
    }
    cv::imshow( "circles", img );
    cv::waitKey(0);

#endif


#if 0
    cv::Mat out;
    cv::Mat cardTemplate = ImageFiltering::getCircleKernel2D(cardSizeMin, cardSizeMin);
    cv::matchTemplate(procImage, cardTemplate,out,cv::TM_CCORR_NORMED);

    ImageCommon::displayMat(out, true, "Match template");

#endif


#if 0

    std::vector<cv::Vec3f> circles;
    ImageFiltering::detectCircles(procImage, circles, cardSizeMin/2, cardSizeMax/2, 0.4);

    // display result :
    cv::Mat img;
    procImage.copyTo(img);
    for( size_t i = 0; i < circles.size(); i++ )
    {
        cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
        int radius = cvRound(circles[i][2]);
        // draw the circle center
        cv::circle( img, center, 3, cv::Scalar(0,255,0), -1, 8, 0 );
        // draw the circle outline
        cv::circle( img, center, radius, cv::Scalar(0,0,255), 3, 8, 0 );
    }
    cv::imshow( "circles", img );
    cv::waitKey(0);

#endif
