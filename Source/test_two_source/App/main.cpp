
// Std
#include <iostream>
#include <vector>

// Qt
#include <QString>
#include <QDir>
#include <QFile>
#include <qmath.h>

// Opencv
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

// Project
#include "CardDetector.h"
#include "Core/Global.h"
#include "Core/ImageCommon.h"
#include "Core/ImageFiltering.h"


bool VERBOSE = false;

/*!
 * OK - Find circles and mask their content = Find cards
 * OK - Rectify circles and its content = Rectify card geometry
 * - Extract objects from each circle = Extract objects from each card
 *  -- Features : Hu moments
 *
 * - Compare objects
*/

QVector<cv::Mat> detectCards(const cv::Mat & src, int cardSizeMin, int cardSizeMax, bool verbose);

bool less(const std::vector<cv::Point> & c1, const std::vector<cv::Point> & c2)
{
    cv::Rect b1 = cv::boundingRect(c1);
    cv::Rect b2 = cv::boundingRect(c2);
    return b1.area() < b2.area();
}

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
    QStringList filesToOpen = QStringList() << files[0];
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

        // DEBUG:
        //    for (int count=0; count<cards.size();count++)
        //    {
        //        const cv::Mat & card = cards.at(count);
        //        ImageCommon::displayMat(card, true, QString("Card %1").arg(count+1));
        //    }

        // ---- UNIFY SIZE OF THE CARDS
        int nbCards = cards.size();
        int uniDim = (cardSizeMin + cardSizeMax)/2;
        //    int maxDim = qMax(cards[0].cols, cards[0].rows);
        //    for (int count=1; count<nbCards;count++)
        //    {
        //        const cv::Mat & card = cards.at(count);
        //        if (maxDim < card.cols)
        //        {
        //            maxDim = card.cols;
        //        }
        //        if (maxDim < card.rows)
        //        {
        //            maxDim = card.rows;
        //        }
        //    }

        SD_TRACE(QString("Uniform size : %1, %2").arg(uniDim).arg(uniDim));

        QVector<cv::Mat> uniCards = cardDetector.uniformSize(cards, uniDim);

        // ---- EXTRACT OBJECT FEATURES FROM EACH CARD
        cv::Mat card = uniCards[1];
        ImageCommon::displayMat(card, true, QString("Card"));

        {
            cv::Size uniSize = card.size();
            //    int objMinSize = uniSize.width*0.1;

            VERBOSE = true;
            cv::Mat procImage;
            card.copyTo(procImage);

            if (procImage.channels() > 1)
            {
                cv::cvtColor(procImage, procImage, cv::COLOR_BGR2GRAY);
            }

            // Canny
            cv::Canny(procImage, procImage, 20, 150);
            if (VERBOSE) ImageCommon::displayMat(procImage, true, "Canny");

            // Morpho
            cv::Mat k1 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3,3));
            cv::morphologyEx(procImage, procImage, cv::MORPH_CLOSE, k1);
            if (VERBOSE) ImageCommon::displayMat(procImage, true, "Morpho");


            // Find contours
            std::vector< std::vector<cv::Point> > contours;
            std::vector< std::vector<cv::Point> > out;
            cv::findContours(procImage, contours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);

            int maxArea = 0.25 * M_PI * uniSize.width * uniSize.height;
            int minArea = 16;
            //    int minArea = 0.25 * M_PI * objMinSize*objMinSize;
            int roiRadius = 0.45 * uniSize.width;
            if (VERBOSE) SD_TRACE(QString("Roi radius : %1").arg(roiRadius));
            //    int maxLength = 0.95*uniSize.width * M_PI;

            if (VERBOSE) SD_TRACE(QString("Contours count : %1").arg(contours.size()));
            for (size_t i=0;i<contours.size();i++)
            {
                std::vector<cv::Point> contour = contours[i];
                //        double p = cv::arcLength(contour, true);
                //        if (p < maxLength)
                //        {
                //            double a = cv::contourArea(contour, true);
                cv::Rect brect = cv::boundingRect(contour);
                int a = brect.area();
                int dx = brect.tl().x + brect.width/2 - uniSize.width/2;
                int dy = brect.tl().y + brect.height/2 - uniSize.height/2;
                int maxdim = qMax(brect.width, brect.height);
                // Select contour such that :
                // a) bounding rect of the contour larger min area and smaller 1/4 of card size
                // b) distance between center of the contour and the card center is smaller than card radius
                // c) max dimension of contour is smaller than card radius
                if (a > minArea && a < maxArea &&
                        dx*dx + dy*dy < roiRadius*roiRadius &&
                        maxdim < roiRadius)
                {
                    out.push_back(contour);
                }

                //        }
            }

            if (VERBOSE) SD_TRACE(QString("Selected contours count : %1").arg(out.size()));
            if (VERBOSE) ImageCommon::displayContour(out, card, false, true);

            // Draw filled contours as segmented image:
            cv::Mat m(procImage.rows, procImage.cols, CV_8U, cv::Scalar::all(0));
            for(int idx=0 ; idx < out.size(); idx++)
            {
                cv::Scalar color( idx );
                cv::drawContours( m, out, idx, color, CV_FILLED);
            }
            if (VERBOSE) ImageCommon::displayMat(m, true, "Segmented");

            for (int t=0;t<10;t++)
            {
                procImage = m == t;
                if (VERBOSE) ImageCommon::displayMat(procImage, true, "object 1");
            }

            // sort by size :
            //    std::sort(out.begin(), out.end(), less);

            //    QList< std::vector<double> > objectFeatures;
            //    QList<QRect> objects;
            //    for (size_t i=out.size()-1;i>0;i--)
            //    {
            //        cv::Moments ms = cv::moments(out[i]);
            //        double hu[7];
            //        cv::HuMoments(ms, hu);
            //        cv::Rect b = cv::boundingRect(out[i]);
            //        SD_TRACE(QString("Contour area : %1").arg(b.area()));
            //    }


        }

        return 0;
    }

}

// *******************************************************
// ***********
// *********** TESTS TO REMOVE
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
