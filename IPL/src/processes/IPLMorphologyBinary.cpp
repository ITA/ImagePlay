#include "IPLMorphologyBinary.h"

#include <atomic>

void IPLMorphologyBinary::init()
{
    // init
    _result = NULL;

    // basic settings
    setClassName("IPLMorphologyBinary");
    setTitle("Binary Morphology");
    setCategory(IPLProcess::CATEGORY_MORPHOLOGY);

    // default value
    // 0 0 0
    // 0 1 0
    // 0 0 0
    int nrElements = 9;
    _kernel.clear();
    for(int i=0; i<nrElements; i++)
    {
        _kernel.push_back((i==4 ? 1 : 0));
    }

    _operation = 0;
    _iterations = 1;

    // inputs and outputs
    addInput("Image", IPLImage::IMAGE_BW);
    addOutput("Image", IPLImage::IMAGE_BW);

    // properties
    addProcessPropertyVectorInt("kernel", "Kernel", "", _kernel, IPL_WIDGET_BINARY_MORPHOLOGY);
    addProcessPropertyInt("iterations", "Iterations",
                          "Run the algorithm x times\nCaution: big kernels and too many iterations can take a long time to compute!",
                          _iterations, IPL_WIDGET_SLIDER, 1, 16);
    addProcessPropertyInt("operation", "Operation:Dilate|Erode|Opening|Closing", "", _operation, IPL_WIDGET_RADIOBUTTONS);
}

void IPLMorphologyBinary::destroy()
{
    delete _result;
}

// Both low-level morphology operators can be reduced to logic OR  or AND operations.
// Those, in turn, can be viewed as simple loops with an exit condition:
// We loop over all cells in the kernel and check for a predicate. If the predicate
// is not statisfied, we can exit the loop and color the center pixel accordingly.

// Example: Dilation: If all of the pixels corresponding to an active kernel cell are
//          zero, the center pixel will result in a zero value. Otherwise, the center
//          pixel is set to one. Therefore: T = 1, F = 0

//          For the erosion, the equation behaves exactly the opposite way
//          (i.e. T = 0, F = 1).

template<int T, int F, class CB>
void applyMorphology(IPLImagePlane &src, IPLImagePlane &dst, int iterations, const std::vector<bool> &kernel, CB progressCallback)
{
    int kernelOffset = (int)sqrt((float)kernel.size()) / 2;

    for (int i = 0; i < iterations; ++i)
    {
        #pragma omp parallel for default(shared)
        for (int y = 0; y < src.height(); ++y)
        {
            for (int x = 0; x < src.width(); ++x)
            {
                bool cancel = false;
                auto &pixelValue = dst.p(x,y);
                int i = 0;
                for( int ky=-kernelOffset; !cancel && ky<=kernelOffset; ky++ )
                {
                    for( int kx=-kernelOffset; !cancel && kx<=kernelOffset; kx++ )
                    {
                        if (   x+kx < 0 || x+kx >= src.width()
                            || y+ky < 0 || y+ky >= src.height())
                            continue;

                        auto &p = src.p(x+kx,y+ky);
                        bool mask = kernel[i++];
                        bool pixel = p == (float)T;
                        cancel = mask && pixel;
                    }
                }

                pixelValue = cancel? T : F;
            }
            progressCallback();
        }
        std::swap(src,dst);
    }
    std::swap(src,dst);
}

template<class CB>
void dilate(const IPLImagePlane &src, IPLImagePlane &dst, int iterations, const std::vector<bool> &kernel, CB progressCallback)
{
    IPLImagePlane input(src); //Don't mutate the original image plane
    applyMorphology<1,0>(input,dst,iterations,kernel,progressCallback);
}

template<class CB>
void erode(const IPLImagePlane &src, IPLImagePlane &dst, int iterations, const std::vector<bool> &kernel, CB progressCallback)
{
    IPLImagePlane input(src); //Don't mutate the original image plane
    applyMorphology<0,1>(input,dst,iterations,kernel,progressCallback);
}

template<class CB>
void open(const IPLImagePlane &src, IPLImagePlane &dst, int iterations, const std::vector<bool> &kernel, CB progressCallback)
{
    IPLImagePlane input(src); //Don't mutate the original image plane
    erode (input,dst,iterations,kernel,progressCallback);
    std::swap(input,dst);
    dilate(input,dst,iterations,kernel,progressCallback);
}

template<class CB>
void close(const IPLImagePlane &src, IPLImagePlane &dst, int iterations, const std::vector<bool> &kernel, CB progressCallback)
{
    IPLImagePlane input(src); //Don't mutate the original image plane
    dilate(input,dst,iterations,kernel,progressCallback);
    std::swap(input,dst);
    erode (input,dst,iterations,kernel,progressCallback);
}


bool IPLMorphologyBinary::processInputData(IPLImage* image, int, bool)
{
    // delete previous result
    delete _result;
    _result = NULL;

    int width = image->width();
    int height = image->height();

    // copy constructor doesnt work:
    // _result = new IPLImage(*image);

    _result = new IPLImage( IPLImage::IMAGE_BW, width, height);

    // get properties
//    _propertyMutex.lock();
    _kernel     = getProcessPropertyVectorInt("kernel");
    _iterations = getProcessPropertyInt("iterations");
    _operation  = getProcessPropertyInt("operation");
//    _propertyMutex.unlock();

    /// @todo implement manhattan distance threshold instead
    /// of stupid iterations...

    enum Operation
    {
        DILATE = 0,
        ERODE,
        OPEN,
        CLOSE
    };

    //std::vector<bool> packs its elements bitwise into a vector of
    //bytes. The kernel therefore uses much less cpu cache this way.
    std::vector<bool> kernel;
    kernel.reserve(_kernel.size());
    for (auto &i: _kernel) kernel.emplace_back(i > 0);

    std::atomic<int> progress(0);
    int totalLines = image->height()*_iterations;

    auto updateProgress = [&]() {
        notifyProgressEventHandler(100*((float)++progress)/totalLines);
    };

    switch(_operation)
    {
    case DILATE:
        dilate(*image->plane(0),*_result->plane(0),_iterations,kernel,updateProgress);
        break;
    case ERODE:
        erode (*image->plane(0),*_result->plane(0),_iterations,kernel,updateProgress);
        break;
    case OPEN:
        totalLines *= 2;
        open  (*image->plane(0),*_result->plane(0),_iterations,kernel,updateProgress);
        break;
    case CLOSE:
        totalLines *= 2;
        close (*image->plane(0),*_result->plane(0),_iterations,kernel,updateProgress);
        break;
    }

    return true;
}

IPLImage* IPLMorphologyBinary::getResultData( int )
{
    return _result;
}
