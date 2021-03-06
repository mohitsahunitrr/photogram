/* Copyright 2014 Matthieu Tourne */

#include "photogram.h"
#include "tracks.hpp"

_INITIALIZE_EASYLOGGINGPP

#include "easyexif/exif.h"
#include "features2d.h"
#include "image_pairs.h"
#include "bundle.h"


#define VISUAL_DEBUG 1

void help(void) {
    cout << "Usage: photogram [image list]" << endl;
}

// debug exif data
void print_exif_data(EXIFInfo &data) {
    printf("Camera make       : %s\n", data.Make.c_str());
    printf("Camera model      : %s\n", data.Model.c_str());
    printf("Original date/time: %s\n", data.DateTimeOriginal.c_str());
    printf("Lens focal length : %f mm\n", data.FocalLength);
    printf("Image width       : %d\n", data.ImageWidth);
    printf("Image height      : %d\n", data.ImageHeight);
    printf("GPS Latitude      : %f deg (%f deg, %f min, %f sec %c)\n",
           data.GeoLocation.Latitude,
           data.GeoLocation.LatComponents.degrees,
           data.GeoLocation.LatComponents.minutes,
           data.GeoLocation.LatComponents.seconds,
           data.GeoLocation.LatComponents.direction);
    printf("GPS Longitude     : %f deg (%f deg, %f min, %f sec %c)\n",
           data.GeoLocation.Longitude,
           data.GeoLocation.LonComponents.degrees,
           data.GeoLocation.LonComponents.minutes,
           data.GeoLocation.LonComponents.seconds,
           data.GeoLocation.LonComponents.direction);
    printf("GPS Altitude      : %f m\n", data.GeoLocation.Altitude);
}

// read exif data for a file
int read_exif(const char *filename, EXIFInfo &exif_data) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        cerr << "Can't read file " << filename << endl;
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    unsigned long fsize = ftell(fp);
    rewind(fp);
    unsigned char buf[fsize];
    if (fread(buf, 1, fsize, fp) != fsize) {
        cerr << "Can't read file " << filename << endl;
        return -2;
    }
    fclose(fp);

    int code = exif_data.parseFrom(buf, fsize);

    if (code) {
        cerr << "Error parsing EXIF from file " <<
            filename << ", code: " << code << endl;
        return -3;
    }

    return 0;
}

// TODO (mtourne): grab from a list of cameras
int get_camera_sensor_size(const string camera_model,
                           float &width, float &height) {
    width = 6.17;
    height = 4.55;

    return 0;
}

// get the intrinsic matrix of the camera
int get_k_matrix_from_exif(const char *filename, Mat& img, Mat &K) {
    EXIFInfo exif_data;
    int rc;

    rc = read_exif(filename, exif_data);
    if (rc != 0) {
        return rc;
    }

#ifndef NDEBUG
    print_exif_data(exif_data);
#endif

    assert(exif_data.ImageWidth == img.cols &&
           exif_data.ImageHeight == img.rows);

    // ccd size in mm
    float width, height;

    rc = get_camera_sensor_size(exif_data.Model, width, height);
    if (rc != 0) {
        return rc;
    }

    // half fov in both directions
    // tan(tetha/2) = width / 2f
    double tan_half_x_fov = width / (2 * exif_data.FocalLength);
    double tan_half_y_fov = height / (2 * exif_data.FocalLength);

    // focal length in pixels :
    // f = (W/2) * (1 / tan(tetha/2))
    double f_x = img.cols / (2 * tan_half_x_fov);
    double f_y = img.rows / (2 * tan_half_y_fov);

    K = Mat::eye(3, 3, CV_64F);
    K.at<double>(0,0) = f_x;
    K.at<double>(1,1) = f_y;

    // center of the sensor is set at width and height / 2
    K.at<double>(0,2) = img.cols / 2;
    K.at<double>(1,2) = img.rows / 2;

    LOG(DEBUG) << "K intrinsic matrix: " << endl << K << endl;

    return 0;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        help();
        return 1;
    }

    Mat K;

    Bundle image_bundle;

    for (int i = 1; i < argc; i++) {
        Image::ptr img_ptr(new Image(argv[i]));

        // TODO (mtourne): catch exception
        Mat img_gray = img_ptr->get_image_gray();

        // TODO (mtourne): replace with parse_exif_data
        // inside Image obj.
        // get instrinsic camera matrix K
        get_k_matrix_from_exif(argv[i], img_gray, K);
        img_ptr->set_camera_matrix(K);

        image_bundle.add_image(img_ptr);
     }

    LOG(DEBUG) << "Bundle size: " << image_bundle.image_count();

    // XX (mtourne): compare all the images with each other for now
    vector<Image::ptr> images = image_bundle.get_images();
    vector<Image::ptr>::iterator it1;
    vector<Image::ptr>::iterator it2;


    for (it1 = images.begin();
         it1 != images.end();
         ++it1) {

        for (it2 = it1 + 1;
             it2 != images.end();
             ++it2) {

            ImagePair image_pair(*it1, *it2);

            // compute matches in a pair
            if (!image_pair.compute_matches()) {
                continue;
            }

            // compute F matrix from matches with 8 point RANSAC
            if (!image_pair.compute_F_mat()) {
                continue;
            }

#if VISUAL_DEBUG
            image_pair.print_matches();
#endif

            image_bundle.add_pair(image_pair);
        }

    }

    LOG(INFO) << "Kept " << image_bundle.pair_count() << " image pairs.";

    LOG(INFO) << "Serializing to disk";


    FileStorage fsb("bundle.txt", FileStorage::WRITE);

    fsb << "bundle" << image_bundle;
    fsb.release();

    LOG(INFO) << "De serializing bundle";
    Bundle new_bundle;

    fsb.open("bundle.txt", FileStorage::READ);
    fsb["bundle"] >> new_bundle;
    fsb.release();

    return 0;
}
