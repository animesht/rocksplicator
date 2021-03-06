/// Copyright 2016 Pinterest Inc.
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
/// http://www.apache.org/licenses/LICENSE-2.0

/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.

//
// @author shu (shu@pinterest.com)
//

#pragma once

#include <aws/s3/S3Client.h>
#include <aws/s3/S3Endpoint.h>
#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/http/HttpClient.h>
#include <aws/core/http/HttpResponse.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/core/utils/Outcome.h>
#include <boost/iostreams/categories.hpp>

#include <iosfwd>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "gflags/gflags.h"

using std::map;
using std::string;
using std::vector;
using std::tuple;
using std::shared_ptr;
using Aws::SDKOptions;
using Aws::Client::ClientConfiguration;
using Aws::Client::XmlOutcome;
using Aws::Http::HttpMethod;
using Aws::S3::S3Client;
using Aws::S3::S3Endpoint::ForRegion;


DECLARE_int32(direct_io_buffer_n_pages);

namespace common {

template <class T>
class S3UtilResponse {
 public:
  S3UtilResponse(T body, string error):
    body_(std::move(body)), error_(std::move(error)) {}

  const T& Body() const {
    return body_;
  }

  const string& Error() const {
    return error_;
  }
 private:
  T body_;
  string error_;
};

/**
 * A writable file which uses direct I/O under the hood.
 */
class DirectIOWritableFile {
 public:
  DirectIOWritableFile(const string& file_path);
  ~DirectIOWritableFile();

  // no copy or move
  DirectIOWritableFile(const DirectIOWritableFile&) = delete;
  DirectIOWritableFile& operator=(const DirectIOWritableFile&) = delete;

  std::streamsize write(const char* s, std::streamsize n);

 private:
  // file descriptor
  int fd_;
  uint32_t file_size_;
  // page size aligned buffer
  void* buffer_;
  // buffer offset
  uint32_t offset_;
  // buffer size
  uint32_t buffer_size_;
};

/**
 * A wrapper class of DirectIOWritableFile which can be used together with
 * boost::iostreams::stream to implement i{o}stream.
 */
class DirectIOFileSink {
 public:
  using char_type = char;
  using category = boost::iostreams::bidirectional_device_tag;

  DirectIOFileSink(const string& file_path)
      : writable_file_(std::make_shared<DirectIOWritableFile>(file_path)) {
  }

  std::streamsize write(const char* s, std::streamsize n) {
    return writable_file_->write(s, n);
  }

  std::streamsize read(char* s, std::streamsize n) {
    // read is currently not implemented because we use this class
    // as ResponseStream which is write only.
    return -1;
  }

 private:
  // boost requires sink class to be copy construtible,
  // hince we use a shared_ptr to manage the underlying
  // DirectIOWritableFile.
  std::shared_ptr<DirectIOWritableFile> writable_file_;
};


using GetObjectResponse = S3UtilResponse<bool>;
using SdkGetObjectResponse = Aws::S3::Model::GetObjectOutcome;
using ListObjectsResponse = S3UtilResponse<vector<string>>;
using GetObjectsResponse = S3UtilResponse<vector<GetObjectResponse>>;
using GetObjectMetadataResponse = S3UtilResponse<map<string, string>>;


class S3Util {
 public:
  /**
   * A wrapper of S3Client so we can control the HTTP request and
   * response directly
   */
  class CutomizedS3Client: public S3Client {
   public:
    CutomizedS3Client(
            const ClientConfiguration& config): S3Client(config) {}
    XmlOutcome MakeHttpRequest(const Aws::String& uri,
            const Aws::AmazonWebServiceRequest& request,
            HttpMethod method = HttpMethod::HTTP_POST) const {
      return MakeRequest(uri, request, method);
    }
  };

  // Don't recommend using this directly. Using BuildS3Util instead.
  // If you must, make sure to call Aws::InitAPI(options); before constructor
  S3Util(const string& bucket,
         const ClientConfiguration& client_config,
         const SDKOptions& options) :
    bucket_(std::move(bucket)), s3Client(client_config), options_(options) {

    Aws::StringStream ss;
    ss << Aws::Http::SchemeMapper::ToString(client_config.scheme) << "://";

    if(client_config.endpointOverride.empty()) {
      ss << ForRegion(client_config.region);
    } else {
      ss << client_config.endpointOverride;
    }
    uri_ = ss.str();
  }

  ~S3Util() {
    Aws::ShutdownAPI(options_);
  }

  // Download an S3 Object to a local file
  GetObjectResponse getObject(const string& key, const string& local_path,
                              const bool direct_io = false);
  // Get object using s3client
  SdkGetObjectResponse sdkGetObject(const string& key,
                                    const string& local_path = "",
                                    const bool direct_io = false);
  // Return a list of objects under the prefix.
  ListObjectsResponse listObjects(const string& prefix);
  // Download all objects under a prefix. We only assume
  // For each object downloading,
  // if the download is successful, the error message will be
  // the object key.
  // If not true, the error message will be the error message.
  GetObjectsResponse getObjects(
      const string& prefix, const string& local_directory,
      const string& delimiter = "/",
      const bool direct_io = false);
  // Get the metadata dict of an object.
  // Now contains md5 and content-length of the s3 object
  GetObjectMetadataResponse getObjectMetadata(const string& key);

  // Some utility methods
  // Given an s3 full path like "s3://<bucket>/<path>",
  // return a tuple of bucketname and file path.
  static tuple<string, string> parseFullS3Path(const string& s3_path);

  static shared_ptr<S3Util> BuildS3Util(
      const uint32_t read_ratelimit_mb = 50,
      const string& bucket = "",
      const uint32_t connect_timeout_ms = 60000,
      const uint32_t request_timeout_ms = 60000);


 private:
  const string bucket_;
  // S3Client is thread safe:
  // https://github.com/aws/aws-sdk-cpp/issues/166
  CutomizedS3Client s3Client;
  SDKOptions options_;
  std::string uri_;
};
}  // namespace common
