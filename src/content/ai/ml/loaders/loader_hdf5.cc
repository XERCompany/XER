#include "loader_hdf5.h"

#include <stdexcept>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#if __has_include(<hdf5/serial/H5Cpp.h>)
#  include <hdf5/serial/H5Cpp.h>
#  define LOADER_HDF5_AVAILABLE 1
#elif __has_include(<H5Cpp.h>)
#  include <H5Cpp.h>
#  define LOADER_HDF5_AVAILABLE 1
#endif

namespace XER::ML::Loaders {

Dataset LoaderHdf5::Load(const std::string& path) {
#ifdef LOADER_HDF5_AVAILABLE
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    try {
        H5::H5File file(path, H5F_ACC_RDONLY);
        H5::DataSet xds = file.openDataSet("X");
        H5::DataSpace sp = xds.getSpace();

        hsize_t dims[2] = {0, 0};
        if (sp.getSimpleExtentDims(dims, nullptr) < 2)
            throw std::runtime_error("Hdf5Loader: dataset 'X' must be 2-D");

        const hsize_t n_rows = dims[0], n_cols = dims[1];
        std::vector<float> flat(n_rows * n_cols);
        xds.read(flat.data(), H5::PredType::NATIVE_FLOAT);

        Dataset ds;
        ds.X.resize(n_rows, std::vector<float>(n_cols));
        for (hsize_t i = 0; i < n_rows; ++i)
            for (hsize_t j = 0; j < n_cols; ++j)
                ds.X[i][j] = flat[i * n_cols + j];

        try {
            H5::DataSet yds = file.openDataSet("y");
            ds.y.resize(n_rows);
            yds.read(ds.y.data(), H5::PredType::NATIVE_INT);
        } catch (const H5::Exception&) {}

        StoreCachedDataset(Name(), path, ds);
        return ds;
    } catch (const H5::Exception& ex) {
        throw std::runtime_error(std::string("Hdf5Loader: ") + ex.getCDetailMsg());
    }
#else
    (void)path;
    return Dataset{};
#endif
}

}  // namespace XER::ML::Loaders
