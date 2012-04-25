#ifndef __SCENE_PROJECTION_MODEL_H__
#define __SCENE_PROJECTION_MODEL_H__

#include "scene/Types.h"
#include "scene/GridGeometry.h"
#include <import/math/poly.h>

namespace scene
{

const double DELTA_GP_MAX = 0.001;

class ProjectionModel
{
protected:
    Vector3 mSlantPlaneNormal;
    Vector3 mImagePlaneRowVector;
    Vector3 mImagePlaneColVector;
    Vector3 mImagePlaneNormal;
    Vector3 mSCP;
    double mScaleFactor;
    math::poly::OneD<Vector3> mARPPoly;
    math::poly::OneD<Vector3> mARPVelPoly;
    math::poly::TwoD<double> mTimeCOAPoly;
    int mLookDir;
    Vector3 mARPPosOffset;
    Vector3 mARPVelOffset;
    double mRgBiasOffset;
public:

    enum { MAX_ITER = 50 };

    ProjectionModel(const Vector3& slantPlaneNormal,
                    const Vector3& imagePlaneRowVector,
                    const Vector3& imagePlaneColVector,
                    const Vector3& scp,
                    const math::poly::OneD<Vector3>& arpPoly,
                    const math::poly::TwoD<double>& timeCOAPoly,
                    int lookDir);

    virtual ~ProjectionModel() {}

    /*!
     *  Utility function that evaluates the TimeCOAPoly at the
     *  given pixel.
     */
    inline double computeImageTime(const RowCol<double> pixel) const
    {
        return mTimeCOAPoly(pixel.row, pixel.col);
    }

    /*!
     *  Utility function that evaluates the ARPPoly at the
     *  given time.
     */
    inline Vector3 computeARPPosition(const double time) const
    {
        return mARPPoly(time);
    }

    /*!
     *  Utility function that evaluates the ARPVelPoly at the
     *  given time.
     */
    inline Vector3 computeARPVelocity(const double time) const
    {
        return mARPVelPoly(time);
    }

    /*!
     *  Implements the transform from a plane point to row column
     *  by subtracting of the SCP projecting into row and column
     *  contributions.
     */
    inline RowCol<double>
        computeImageCoordinates(const Vector3& imagePlanePoint) const
    {
        // Delta IPP = xrow * uRow + ycol * uCol
        Vector3 delta(imagePlanePoint - mSCP);
            
        // What is the x contribution?
        return RowCol<double>(delta.dot(mImagePlaneRowVector),
                                    delta.dot(mImagePlaneColVector) );

    }

        
    /*!
     *  Virtual method to compute the R/Rdot contour from an
     *  image grid point.  Described in Chapter 4 of
     *  SICD Image Projections.  This routine is specific to IFP
     *  algorithm and to grid type, and so is provided in sub-classes
     *
     */
    virtual void computeContour(const Vector3& arpCOA,
                                const Vector3& velCOA,
                                double timeCOA,
                                const RowCol<double>& imageGridPoint,
                                double* r,
                                double* rDot) const = 0;
        
    /*!
     *  Calculations for section 5.2 in SICD Image Projections:
     *  R/Rdot Contour Ground Plane Intersection
     *
     */
    Vector3
        contourToGroundPlane(double rCOA, double rDotCOA,
                             const Vector3& arpCOA,
                             const Vector3& velCOA,
                             double timeCOA,
                             const Vector3& groundPlaneNormal,
                             const Vector3& groundRefPoint) const;

    /*!
     *  Implements chapter 6 Precise Scene to Image Grid
     *  Projection from SICD Image Projections, 6.1
     *  Scene to Image: Single Scene Point
     *
     *  This method is iterative.  Using a scenePoint, it iteratively
     *  projects this point into an image plane point, and
     *  converting that to a rg/az coord.  These rg/az coords
     *  are used in a IFP algorithm/grid-type specific way to
     *  find the range/range-rate contour (called R/Rdot in the doc).
     *  Finally this is used to perform a projection to the ground plane
     *  and compare with the reference until convergence is reached.
     *
     *  Note that in the document, Scene and Ground are used
     *  interchangeably,
     *  and sometimes even Image and Slant are as well.
     *
     *  \param scenePoint A scene (ground) point in 3-space
     *  \param oTimeCOA An optional ptr to the timeCOA,
     *  which, if NULL is not set.
     *  \return An continuous surface image point
     *
     */
    RowCol<double> sceneToImage(const Vector3& scenePoint,
                                      double* oTimeCOA) const;


    /*!
     *  Implements (Slant plane) Image to Scene (Ground plane)
     *  projection using computerContour and contourToGroundPlane
     *
     *  \param imagePoint A point in the image surface (continuous)
     *  \param A ground plane reference point
     *  \param groundPlaneNormal A parameter which could just be the
     *  normalized ground plane reference point
     *  \param oTimeCOA [output] optional evaluation of TimeCOAPoly at
     *                  imageGridPoint.row, imageGridPoint.col
     *  \return A scene (ground) point in 3 space
     */
    Vector3 imageToScene(const RowCol<double>& imageGridPoint,
                                const Vector3& groundRefPoint,
                                const Vector3& groundPlaneNormal,
                                double *oTimeCOA) const;

    /*!
     * Samples a 10x10 grid of points that spans outExtent using
     * imageToScene().  From these samples, fits projection and time COA
     * polynomials of the specified order.  Optionally computes mean residual
     * errors in these polynomials (mean of the squares of the differences).
     *
     * \param gridGeom Grid geometry
     * \param inPixelStart Input space start pixel (i.e. if this is non-zero,
     * it indicates an AOI)
     * \param inSceneCenter Input space scene center pixel in row/col
     * \param interimSceneCenter Scene center pixel in row/col of the image
     * when outputToSlantRow/Col polynomials are applied
     * \param interimSampleSpacing Sample spacing of the image when
     * outputToSlantRow/Col polynomials are applied
     * \param outSceneCenter Output space scene center pixel in row/col
     * \param outSampleSpacing Output space sample spacing
     * \param outExtent Output extent in row/col
     * \param polyOrder Polynomial order to use when fitting the polynomials
     * \param outputToSlantRow [output] Output to slant row polynomial
     * \param outputToSlantCol [output] Output to slant col polynomial
     * \param timeCOAPoly [output] Time center of aperture polynomial
     * \param meanResidualErrorRow [output] Optional.  Mean residual error in
     * outputToSlantRow
     * \param meanResidualErrorCol [output] Optional.  Mean residual error in
     * outputToSlantCol
     * \param meanResidualErrorTCOA [output] Optional.  Mean residual error in
     * timeCOAPoly
     */
    void computeProjectionPolynomials(
        const scene::GridGeometry& gridGeom,
        const RowCol<size_t>& inPixelStart,
        const RowCol<double>& inSceneCenter,
        const RowCol<double>& interimSceneCenter,
        const RowCol<double>& interimSampleSpacing,
        const RowCol<double>& outSceneCenter,
        const RowCol<double>& outSampleSpacing,
        const RowCol<size_t>& outExtent,
        size_t polyOrder,
        math::poly::TwoD<double>& outputToSlantRow,
        math::poly::TwoD<double>& outputToSlantCol,
        math::poly::TwoD<double>& timeCOAPoly,
        double* meanResidualErrorRow = NULL,
        double* meanResidualErrorCol = NULL,
        double* meanResidualErrorTCOA = NULL) const;


    /*!
     *  Functions needed to support adjustable parameter offsets
     */
    void setARPPositionOffset(Vector3 offset)
    {
        mARPPosOffset = offset;
    }

    void setARPVelocityOffset(Vector3 offset)
    {
        mARPVelOffset = offset;
    }

    void setRangeBiasOffset(double offset)
    {
        mRgBiasOffset = offset;
    }

};

class RangeAzimProjectionModel : public ProjectionModel
{
protected:
    math::poly::OneD<double> mPolarAnglePoly;
    math::poly::OneD<double> mPolarAnglePolyPrime;
    math::poly::OneD<double> mKSFPoly;
    math::poly::OneD<double> mKSFPolyPrime;

public:
    RangeAzimProjectionModel(const math::poly::OneD<double>& polarAnglePoly,
                             const math::poly::OneD<double>& ksfPoly,
                             const Vector3& slantPlaneNormal,
                             const Vector3& imagePlaneRowVector,
                             const Vector3& imagePlaneColVector,
                             const Vector3& scp,
                             const math::poly::OneD<Vector3>& arpPoly,
                             const math::poly::TwoD<double>& timeCOAPoly,
                             int lookDir);

    virtual ~RangeAzimProjectionModel() {}

    virtual void computeContour(const Vector3& arpCOA,
                                const Vector3& velCOA,
                                double timeCOA,
                                const RowCol<double>& imageGridPoint,
                                double* r,
                                double* rDot) const;

};

class RangeZeroProjectionModel : public ProjectionModel
{
protected:
    math::poly::OneD<double> mTimeCAPoly;
    math::poly::TwoD<double> mDSRFPoly;
    double mRangeCA;
    
public:
    RangeZeroProjectionModel(const math::poly::OneD<double>& timeCAPoly,
                             const math::poly::TwoD<double>& dsrfPoly,
                             double rangeCA,
                             const Vector3& slantPlaneNormal,
                             const Vector3& imagePlaneRowVector,
                             const Vector3& imagePlaneColVector,
                             const Vector3& scp,
                             const math::poly::OneD<Vector3>& arpPoly,
                             const math::poly::TwoD<double>& timeCOAPoly,
                             int lookDir);

    virtual ~RangeZeroProjectionModel() {}

    virtual void computeContour(const Vector3& arpCOA,
                                const Vector3& velCOA,
                                double timeCOA,
                                const RowCol<double>& imageGridPoint,
                                double* r,
                                double* rDot) const;

};


class PlaneProjectionModel : public ProjectionModel
{
public:
    PlaneProjectionModel(const Vector3& slantPlaneNormal,
                         const Vector3& imagePlaneRowVector,
                         const Vector3& imagePlaneColVector,
                         const Vector3& scp,
                         const math::poly::OneD<Vector3>& arpPoly,
                         const math::poly::TwoD<double>& timeCOAPoly,
                         int lookDir);

    virtual ~PlaneProjectionModel() {}

    virtual void computeContour(const Vector3& arpCOA,
                                const Vector3& velCOA,
                                double timeCOA,
                                const RowCol<double>& imageGridPoint,
                                double* r,
                                double* rDot) const;

};

typedef PlaneProjectionModel XRGYCRProjectionModel;
typedef PlaneProjectionModel XCTYATProjectionModel;
}

#endif
