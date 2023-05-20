#pragma once
#include <math.h>
// Simple math helper structs

// Matrix4x4
// elements are stored in column-major order
struct Matrix4x4
{
    float m[16];

    Matrix4x4()
    {
        *this = Identity();
    }

    Matrix4x4(float m00, float m01, float m02, float m03, float m04, float m05, float m06, float m07, float m08,
              float m09, float m10, float m11, float m12, float m13, float m14, float m15)
    {
        m[0] = m00;
        m[1] = m01;
        m[2] = m02;
        m[3] = m03;
        m[4] = m04;
        m[5] = m05;
        m[6] = m06;
        m[7] = m07;
        m[8] = m08;
        m[9] = m09;
        m[10] = m10;
        m[11] = m11;
        m[12] = m12;
        m[13] = m13;
        m[14] = m14;
        m[15] = m15;
    }

    static Matrix4x4 Identity()
    {
        return Matrix4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    }

    // compute the inverse of a 4x4 affine matrix
    void Invert()
    {
        // Create shorthands to access matrix members
        float m00 = m[0], m10 = m[1], m20 = m[2], m30 = m[3];
        float m01 = m[4], m11 = m[5], m21 = m[6], m31 = m[7];
        float m02 = m[8], m12 = m[9], m22 = m[10], m32 = m[11];

        // Invert 3x3 part of the 4x4 matrix that contains the rotation, etc.
        // That part is called R from here on.

        // Calculate cofactor matrix of R
        float c00 = m11 * m22 - m12 * m21, c10 = -(m01 * m22 - m02 * m21), c20 = m01 * m12 - m02 * m11;
        float c01 = -(m10 * m22 - m12 * m20), c11 = m00 * m22 - m02 * m20, c21 = -(m00 * m12 - m02 * m10);
        float c02 = m10 * m21 - m11 * m20, c12 = -(m00 * m21 - m01 * m20), c22 = m00 * m11 - m01 * m10;

        // Caclculate the determinant by using the already calculated determinants
        // in the cofactor matrix.
        // Second sign is already minus from the cofactor matrix.
        float det = m00 * c00 + m10 * c10 + m20 * c20;
        if (fabsf(det) < 0.00001)
            *this = Identity();

        // Calcuate inverse of R by dividing the transposed cofactor matrix by the
        // determinant.
        float i00 = c00 / det, i10 = c01 / det, i20 = c02 / det;
        float i01 = c10 / det, i11 = c11 / det, i21 = c12 / det;
        float i02 = c20 / det, i12 = c21 / det, i22 = c22 / det;

        // Combine the inverted R with the inverted translation
        *this = Matrix4x4(i00, i10, i20, -(i00 * m30 + i10 * m31 + i20 * m32), i01, i11, i21,
                    -(i01 * m30 + i11 * m31 + i21 * m32), i02, i12, i22, -(i02 * m30 + i12 * m31 + i22 * m32), 0, 0, 0,
                    1);
    }

    // operator [] to access elements
    float& operator[](int i)
    {
        return m[i];
    }
};
