#ifndef ImageProcessingTest_H
#define ImageProcessingTest_H

// Qt
#include <QObject>
#include <QtTest>

// Project

namespace Tests
{

//*************************************************************************

class ImageProcessingTest : public QObject
{
    Q_OBJECT
private slots:

    void detectObjectsTest1();
    void detectObjectsTest2();
    void detectObjectsTest3();

private:

};

//*************************************************************************

} 

#endif // ImageProcessingTest_H
