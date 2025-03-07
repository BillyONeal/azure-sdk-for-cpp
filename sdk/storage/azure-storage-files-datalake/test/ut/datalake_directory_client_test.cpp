// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "datalake_directory_client_test.hpp"

#include <algorithm>
#include <thread>

#include <azure/identity/client_secret_credential.hpp>
#include <azure/storage/common/internal/shared_key_policy.hpp>

namespace Azure { namespace Storage { namespace Test {

  void DataLakeDirectoryClientTest::SetUp()
  {
    DataLakeFileSystemClientTest::SetUp();
    CHECK_SKIP_TEST();

    m_directoryName = GetFileSystemValidName();
    m_directoryClient = std::make_shared<Files::DataLake::DataLakeDirectoryClient>(
        m_fileSystemClient->GetDirectoryClient(m_directoryName));
    m_fileSystemClient->GetFileClient(m_directoryName).Create();
  }

  void DataLakeDirectoryClientTest::TearDown()
  {
    CHECK_SKIP_TEST();
    m_fileSystemClient->GetFileClient(m_directoryName).Delete();
    DataLakeFileSystemClientTest::TearDown();
  }

  TEST_F(DataLakeDirectoryClientTest, CreateDeleteDirectory)
  {
    const std::string testName(GetTestName());
    {
      // Normal create/delete.
      std::vector<Files::DataLake::DataLakeDirectoryClient> directoryClient;
      for (int32_t i = 0; i < 5; ++i)
      {
        auto client = m_fileSystemClient->GetDirectoryClient(testName + std::to_string(i));
        EXPECT_NO_THROW(client.Create());
        directoryClient.emplace_back(std::move(client));
      }
      for (const auto& client : directoryClient)
      {
        EXPECT_NO_THROW(client.DeleteEmpty());
      }
    }
    {
      // Normal delete with last modified access condition.
      std::vector<Files::DataLake::DataLakeDirectoryClient> directoryClient;
      for (int32_t i = 0; i < 2; ++i)
      {
        auto client = m_fileSystemClient->GetDirectoryClient(testName + "2" + std::to_string(i));
        EXPECT_NO_THROW(client.Create());
        directoryClient.emplace_back(std::move(client));
      }
      for (const auto& client : directoryClient)
      {
        auto response = client.GetProperties();
        Files::DataLake::DeleteDirectoryOptions options1;
        options1.AccessConditions.IfModifiedSince = response.Value.LastModified;
        EXPECT_TRUE(IsValidTime(response.Value.LastModified));
        EXPECT_THROW(client.DeleteEmpty(options1), StorageException);
        Files::DataLake::DeleteDirectoryOptions options2;
        options2.AccessConditions.IfUnmodifiedSince = response.Value.LastModified;
        EXPECT_NO_THROW(client.DeleteEmpty(options2));
      }
    }
    {
      // Normal delete with if match access condition.
      std::vector<Files::DataLake::DataLakeDirectoryClient> directoryClient;
      for (int32_t i = 0; i < 2; ++i)
      {
        auto client = m_fileSystemClient->GetDirectoryClient(testName + "3" + std::to_string(i));
        EXPECT_NO_THROW(client.Create());
        directoryClient.emplace_back(std::move(client));
      }
      for (const auto& client : directoryClient)
      {
        auto response = client.GetProperties();
        EXPECT_TRUE(response.Value.IsDirectory);
        Files::DataLake::DeleteDirectoryOptions options1;
        options1.AccessConditions.IfNoneMatch = response.Value.ETag;
        EXPECT_THROW(client.DeleteEmpty(options1), StorageException);
        Files::DataLake::DeleteDirectoryOptions options2;
        options2.AccessConditions.IfMatch = response.Value.ETag;
        EXPECT_NO_THROW(client.DeleteEmpty(options2));
      }
    }

    {
      // Recursive delete works.
      std::vector<Files::DataLake::DataLakeDirectoryClient> directoryClient;
      auto rootDir = testName + "root";
      auto rootDirClient = m_fileSystemClient->GetDirectoryClient(rootDir);
      EXPECT_NO_THROW(rootDirClient.Create());
      for (int32_t i = 0; i < 5; ++i)
      {
        auto client = m_fileSystemClient->GetDirectoryClient(rootDir + "/d" + std::to_string(i));
        EXPECT_NO_THROW(client.Create());
        directoryClient.emplace_back(std::move(client));
      }
      EXPECT_THROW(rootDirClient.DeleteEmpty(), StorageException);
      EXPECT_NO_THROW(rootDirClient.DeleteRecursive());
    }
  }

  TEST_F(DataLakeDirectoryClientTest, CreateDeleteIfExistsDirectory)
  {
    std::string const testName(GetTestName());
    {
      auto client = m_fileSystemClient->GetDirectoryClient(testName + "1");
      bool created = false;
      bool deleted = false;
      EXPECT_NO_THROW(created = client.Create().Value.Created);
      EXPECT_TRUE(created);
      EXPECT_NO_THROW(created = client.CreateIfNotExists().Value.Created);
      EXPECT_FALSE(created);
      EXPECT_NO_THROW(deleted = client.DeleteEmpty().Value.Deleted);
      EXPECT_TRUE(deleted);
      EXPECT_NO_THROW(deleted = client.DeleteEmptyIfExists().Value.Deleted);
      EXPECT_FALSE(deleted);
    }
    {
      auto client = Files::DataLake::DataLakeDirectoryClient::CreateFromConnectionString(
          AdlsGen2ConnectionString(),
          GetTestNameLowerCase(),
          testName + "2",
          InitClientOptions<Files::DataLake::DataLakeClientOptions>());
      bool deleted = false;
      EXPECT_NO_THROW(deleted = client.DeleteEmptyIfExists().Value.Deleted);
      EXPECT_FALSE(deleted);
    }
  }

  TEST_F(DataLakeDirectoryClientTest, RenameFile)
  {
    const std::string testName(GetTestName());
    const std::string baseDirectoryName = testName + "1";
    auto baseDirectoryClient = m_fileSystemClient->GetDirectoryClient(baseDirectoryName);
    baseDirectoryClient.Create();

    const std::string oldFilename = testName + "2";
    auto oldFileClient = baseDirectoryClient.GetSubdirectoryClient(oldFilename);
    oldFileClient.Create();
    const std::string newFilename = testName + "3";
    auto newFileClient
        = baseDirectoryClient.RenameFile(oldFilename, baseDirectoryName + "/" + newFilename).Value;
    EXPECT_NO_THROW(newFileClient.GetProperties());
    EXPECT_NO_THROW(baseDirectoryClient.GetSubdirectoryClient(newFilename).GetProperties());
    EXPECT_THROW(oldFileClient.GetProperties(), StorageException);

    const std::string newFileSystemName = GetTestNameLowerCase() + "1";
    const std::string newFilename2 = testName + "4";

    auto newFileSystem = m_dataLakeServiceClient->GetFileSystemClient(newFileSystemName);
    newFileSystem.Create();

    Files::DataLake::RenameFileOptions options;
    options.DestinationFileSystem = newFileSystemName;
    auto newFileClient2 = baseDirectoryClient.RenameFile(newFilename, newFilename2, options).Value;

    EXPECT_NO_THROW(newFileClient2.GetProperties());
    EXPECT_NO_THROW(newFileSystem.GetFileClient(newFilename2).GetProperties());
    newFileSystem.Delete();
    EXPECT_THROW(newFileClient.GetProperties(), StorageException);
  }

  TEST_F(DataLakeDirectoryClientTest, RenameFileSasAuthentication_LIVEONLY_)
  {
    const std::string testName(GetTestName());
    const std::string sourceFilename = testName + "1";
    const std::string destinationFilename = testName + "2";
    auto baseDirectoryClient = m_fileSystemClient->GetDirectoryClient("based");
    baseDirectoryClient.Create();
    auto fileClient = baseDirectoryClient.GetFileClient(sourceFilename);
    fileClient.CreateIfNotExists();

    Files::DataLake::DataLakeDirectoryClient directoryClientSas(
        Files::DataLake::_detail::GetDfsUrlFromUrl(baseDirectoryClient.GetUrl()) + GetSas());
    directoryClientSas.RenameFile(sourceFilename, destinationFilename);
    EXPECT_THROW(
        baseDirectoryClient.GetFileClient(sourceFilename).GetProperties(), StorageException);
    EXPECT_NO_THROW(m_fileSystemClient->GetFileClient(destinationFilename).GetProperties());

    const std::string sourceDirectoryName = testName + "3";
    const std::string destinationDirectoryName = testName + "4";
    auto directoryClient = baseDirectoryClient.GetSubdirectoryClient(sourceDirectoryName);
    directoryClient.CreateIfNotExists();

    directoryClientSas.RenameSubdirectory(sourceDirectoryName, destinationDirectoryName);
    EXPECT_THROW(
        baseDirectoryClient.GetSubdirectoryClient(sourceDirectoryName).GetProperties(),
        StorageException);
    EXPECT_NO_THROW(
        m_fileSystemClient->GetDirectoryClient(destinationDirectoryName).GetProperties());
  }

  TEST_F(DataLakeDirectoryClientTest, RenameFileAccessCondition)
  {
    const std::string testName(GetTestName());

    const std::string baseDirectoryName = testName + "1";
    auto baseDirectoryClient = m_fileSystemClient->GetDirectoryClient(baseDirectoryName);
    baseDirectoryClient.Create();

    const std::string oldFilename = testName + "2";
    auto oldFileClient = baseDirectoryClient.GetSubdirectoryClient(oldFilename);
    oldFileClient.Create();
    const std::string newFilename = testName + "3";

    Files::DataLake::RenameFileOptions options;
    options.SourceAccessConditions.IfModifiedSince
        = oldFileClient.GetProperties().Value.LastModified;
    EXPECT_THROW(
        baseDirectoryClient.RenameFile(oldFilename, newFilename, options), StorageException);

    options = Files::DataLake::RenameFileOptions();
    options.SourceAccessConditions.IfUnmodifiedSince
        = oldFileClient.GetProperties().Value.LastModified - std::chrono::minutes(5);

    EXPECT_THROW(
        baseDirectoryClient.RenameFile(oldFilename, newFilename, options), StorageException);

    options = Files::DataLake::RenameFileOptions();
    options.SourceAccessConditions.IfMatch = DummyETag;

    EXPECT_THROW(
        baseDirectoryClient.RenameFile(oldFilename, newFilename, options), StorageException);

    options = Files::DataLake::RenameFileOptions();
    options.SourceAccessConditions.IfNoneMatch = oldFileClient.GetProperties().Value.ETag;

    EXPECT_THROW(
        baseDirectoryClient.RenameFile(oldFilename, newFilename, options), StorageException);
  }

  TEST_F(DataLakeDirectoryClientTest, RenameDirectory)
  {
    const std::string testName(GetTestName());

    const std::string baseDirectoryName = testName + "1";
    auto baseDirectoryClient = m_fileSystemClient->GetDirectoryClient(baseDirectoryName);
    baseDirectoryClient.Create();

    const std::string oldDirectoryName = testName + "2";
    auto oldDirectoryClient = baseDirectoryClient.GetSubdirectoryClient(oldDirectoryName);
    oldDirectoryClient.Create();
    const std::string newDirectoryName = testName + "3";
    auto newDirectoryClient
        = baseDirectoryClient
              .RenameSubdirectory(oldDirectoryName, baseDirectoryName + "/" + newDirectoryName)
              .Value;
    EXPECT_NO_THROW(newDirectoryClient.GetProperties());
    EXPECT_NO_THROW(baseDirectoryClient.GetSubdirectoryClient(newDirectoryName).GetProperties());
    EXPECT_THROW(oldDirectoryClient.GetProperties(), StorageException);

    const std::string newFileSystemName = GetTestNameLowerCase();
    const std::string newDirectoryName2 = testName + "4";

    auto newFileSystem = m_dataLakeServiceClient->GetFileSystemClient(newFileSystemName);
    newFileSystem.Create();

    Files::DataLake::RenameDirectoryOptions options;
    options.DestinationFileSystem = newFileSystemName;
    auto newDirectoryClient2
        = baseDirectoryClient.RenameSubdirectory(newDirectoryName, newDirectoryName2, options)
              .Value;

    EXPECT_NO_THROW(newDirectoryClient2.GetProperties());
    EXPECT_NO_THROW(newFileSystem.GetDirectoryClient(newDirectoryName2).GetProperties());
    newFileSystem.Delete();
    EXPECT_THROW(newDirectoryClient.GetProperties(), StorageException);
  }

  TEST_F(DataLakeDirectoryClientTest, RenameDirectoryAccessCondition)
  {
    const std::string testName(GetTestName());

    const std::string baseDirectoryName = testName + "1";
    auto baseDirectoryClient = m_fileSystemClient->GetDirectoryClient(baseDirectoryName);
    baseDirectoryClient.Create();

    const std::string oldDirectoryName = testName + "2";
    auto oldDirectoryClient = baseDirectoryClient.GetSubdirectoryClient(oldDirectoryName);
    oldDirectoryClient.Create();
    const std::string newDirectoryName = testName + "3";

    Files::DataLake::RenameDirectoryOptions options;
    options.SourceAccessConditions.IfModifiedSince
        = oldDirectoryClient.GetProperties().Value.LastModified;
    EXPECT_THROW(
        baseDirectoryClient.RenameSubdirectory(oldDirectoryName, newDirectoryName, options),
        StorageException);

    options = Files::DataLake::RenameDirectoryOptions();
    options.SourceAccessConditions.IfUnmodifiedSince
        = oldDirectoryClient.GetProperties().Value.LastModified - std::chrono::minutes(5);

    EXPECT_THROW(
        baseDirectoryClient.RenameSubdirectory(oldDirectoryName, newDirectoryName, options),
        StorageException);

    options = Files::DataLake::RenameDirectoryOptions();
    options.SourceAccessConditions.IfMatch = DummyETag;

    EXPECT_THROW(
        baseDirectoryClient.RenameSubdirectory(oldDirectoryName, newDirectoryName, options),
        StorageException);

    options = Files::DataLake::RenameDirectoryOptions();
    options.SourceAccessConditions.IfNoneMatch = oldDirectoryClient.GetProperties().Value.ETag;

    EXPECT_THROW(
        baseDirectoryClient.RenameSubdirectory(oldDirectoryName, newDirectoryName, options),
        StorageException);
  }

  TEST_F(DataLakeDirectoryClientTest, DirectoryMetadata)
  {
    auto metadata1 = GetMetadata();
    auto metadata2 = GetMetadata();
    {
      // Set/Get Metadata works
      EXPECT_NO_THROW(m_directoryClient->SetMetadata(metadata1));
      auto result = m_directoryClient->GetProperties().Value.Metadata;
      EXPECT_EQ(metadata1, result);
      EXPECT_NO_THROW(m_directoryClient->SetMetadata(metadata2));
      result = m_directoryClient->GetProperties().Value.Metadata;
      EXPECT_EQ(metadata2, result);
    }

    {
      // Create path with metadata works
      const std::string testName(GetTestName());
      auto client1 = m_fileSystemClient->GetDirectoryClient(testName + "1");
      auto client2 = m_fileSystemClient->GetDirectoryClient(testName + "2");
      Files::DataLake::CreatePathOptions options1;
      Files::DataLake::CreatePathOptions options2;
      options1.Metadata = metadata1;
      options2.Metadata = metadata2;

      EXPECT_NO_THROW(client1.Create(options1));
      EXPECT_NO_THROW(client2.Create(options2));
      auto result = client1.GetProperties().Value.Metadata;
      /* cspell:disable-next-line */
      metadata1["hdi_isfolder"] = "true";
      /* cspell:disable-next-line */
      metadata2["hdi_isfolder"] = "true";
      EXPECT_EQ(metadata1, result);
      result = client2.GetProperties().Value.Metadata;
      EXPECT_EQ(metadata2, result);
    }
  }

  TEST_F(DataLakeDirectoryClientTest, DirectoryProperties)
  {
    auto metadata1 = GetMetadata();
    auto metadata2 = GetMetadata();
    {
      // Get Metadata via properties works
      EXPECT_NO_THROW(m_directoryClient->SetMetadata(metadata1));
      auto result = m_directoryClient->GetProperties();
      EXPECT_EQ(metadata1, result.Value.Metadata);
      EXPECT_NO_THROW(m_directoryClient->SetMetadata(metadata2));
      result = m_directoryClient->GetProperties();
      EXPECT_EQ(metadata2, result.Value.Metadata);
    }

    {
      // Last modified Etag works.
      auto properties1 = m_directoryClient->GetProperties();
      auto properties2 = m_directoryClient->GetProperties();
      EXPECT_EQ(properties1.Value.ETag, properties2.Value.ETag);
      EXPECT_TRUE(IsValidTime(properties1.Value.LastModified));
      EXPECT_EQ(properties1.Value.LastModified, properties2.Value.LastModified);

      // This operation changes ETag/LastModified.
      EXPECT_NO_THROW(m_directoryClient->SetMetadata(metadata1));

      auto properties3 = m_directoryClient->GetProperties();
      EXPECT_NE(properties1.Value.ETag, properties3.Value.ETag);
    }

    {
      // HTTP headers works.
      auto httpHeader = GetInterestingHttpHeaders();
      std::vector<Files::DataLake::DataLakeDirectoryClient> directoryClient;
      const std::string testName(GetTestName());
      for (int32_t i = 0; i < 2; ++i)
      {
        auto client = m_fileSystemClient->GetDirectoryClient(testName + std::to_string(i));
        Files::DataLake::CreatePathOptions options;
        options.HttpHeaders = httpHeader;
        EXPECT_NO_THROW(client.Create(options));
        directoryClient.emplace_back(std::move(client));
      }
      for (const auto& client : directoryClient)
      {
        auto result = client.GetProperties();
        EXPECT_EQ(httpHeader.CacheControl, result.Value.HttpHeaders.CacheControl);
        EXPECT_EQ(httpHeader.ContentDisposition, result.Value.HttpHeaders.ContentDisposition);
        EXPECT_EQ(httpHeader.ContentLanguage, result.Value.HttpHeaders.ContentLanguage);
        EXPECT_EQ(httpHeader.ContentType, result.Value.HttpHeaders.ContentType);
        EXPECT_NO_THROW(client.DeleteEmpty());
      }
    }
  }

  TEST_F(DataLakeDirectoryClientTest, DirectoryAccessControlRecursive)
  {
    // Setup directories.
    const std::string testName(GetTestName());
    auto rootDirectoryName = testName + "1";
    auto directoryName1 = testName + "2";
    auto directoryName2 = testName + "3";
    auto rootDirectoryClient = m_fileSystemClient->GetDirectoryClient(rootDirectoryName);
    rootDirectoryClient.Create();
    auto directoryClient1
        = m_fileSystemClient->GetDirectoryClient(rootDirectoryName + "/" + directoryName1);
    directoryClient1.Create();
    auto directoryClient2
        = m_fileSystemClient->GetDirectoryClient(rootDirectoryName + "/" + directoryName2);
    directoryClient2.Create();

    {
      // Set Acls recursive.
      std::vector<Files::DataLake::Models::Acl> acls = GetValidAcls();
      EXPECT_NO_THROW(rootDirectoryClient.SetAccessControlListRecursive(acls));
      std::vector<Files::DataLake::Models::Acl> resultAcls1;
      std::vector<Files::DataLake::Models::Acl> resultAcls2;
      EXPECT_NO_THROW(resultAcls1 = directoryClient1.GetAccessControlList().Value.Acls);
      EXPECT_NO_THROW(resultAcls2 = directoryClient2.GetAccessControlList().Value.Acls);
      for (const auto& acl : resultAcls2)
      {
        auto iter = std::find_if(
            resultAcls1.begin(),
            resultAcls1.end(),
            [&acl](const Files::DataLake::Models::Acl& targetAcl) {
              return (targetAcl.Type == acl.Type) && (targetAcl.Id == acl.Id)
                  && (targetAcl.Scope == acl.Scope);
            });
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ(iter->Permissions, acl.Permissions);
      }
    }
    {
      // Update Acls recursive.
      std::vector<Files::DataLake::Models::Acl> originalAcls = GetValidAcls();
      Files::DataLake::Models::Acl newAcl;
      newAcl.Type = "group";
      newAcl.Id = "";
      newAcl.Permissions = "rw-";
      std::vector<Files::DataLake::Models::Acl> acls;
      acls.emplace_back(std::move(newAcl));
      EXPECT_NO_THROW(rootDirectoryClient.UpdateAccessControlListRecursive(acls));
      std::vector<Files::DataLake::Models::Acl> resultAcls1;
      std::vector<Files::DataLake::Models::Acl> resultAcls2;
      EXPECT_NO_THROW(resultAcls1 = directoryClient1.GetAccessControlList().Value.Acls);
      EXPECT_NO_THROW(resultAcls2 = directoryClient2.GetAccessControlList().Value.Acls);
      for (const auto& acl : resultAcls2)
      {
        auto iter = std::find_if(
            resultAcls1.begin(),
            resultAcls1.end(),
            [&acl](const Files::DataLake::Models::Acl& targetAcl) {
              return (targetAcl.Type == acl.Type) && (targetAcl.Id == acl.Id)
                  && (targetAcl.Scope == acl.Scope);
            });
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ(iter->Permissions, acl.Permissions);
      }
      {
        // verify group has changed
        auto groupFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "group";
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), groupFinder);
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ("rw-", iter->Permissions);
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), groupFinder);
        EXPECT_NE(iter, resultAcls2.end());
        EXPECT_EQ("rw-", iter->Permissions);
      }
      {
        // verify other has not changed
        {
          auto otherFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
            return targetAcl.Type == "other";
          };
          auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), otherFinder);
          EXPECT_NE(iter, resultAcls1.end());
          EXPECT_EQ(originalAcls[3].Permissions, iter->Permissions);
          iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), otherFinder);
          EXPECT_NE(iter, resultAcls2.end());
          EXPECT_EQ(originalAcls[3].Permissions, iter->Permissions);
        }
        {
          auto userFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
            return targetAcl.Type == "user";
          };
          auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), userFinder);
          EXPECT_NE(iter, resultAcls1.end());
          if (iter->Id == originalAcls[0].Id)
          {
            EXPECT_EQ(originalAcls[0].Permissions, iter->Permissions);
          }
          else
          {
            EXPECT_EQ(originalAcls[1].Permissions, iter->Permissions);
          }
          iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), userFinder);
          EXPECT_NE(iter, resultAcls2.end());
          if (iter->Id == originalAcls[0].Id)
          {
            EXPECT_EQ(originalAcls[0].Permissions, iter->Permissions);
          }
          else
          {
            EXPECT_EQ(originalAcls[1].Permissions, iter->Permissions);
          }
        }
      }
    }
    {
      // Remove Acls recursive.
      std::vector<Files::DataLake::Models::Acl> originalAcls = GetValidAcls();
      Files::DataLake::Models::Acl removeAcl;
      removeAcl.Type = "user";
      removeAcl.Id = "72a3f86f-271f-439e-b031-25678907d381";
      std::vector<Files::DataLake::Models::Acl> acls;
      acls.emplace_back(std::move(removeAcl));
      EXPECT_NO_THROW(rootDirectoryClient.RemoveAccessControlListRecursive(acls));
      std::vector<Files::DataLake::Models::Acl> resultAcls1;
      std::vector<Files::DataLake::Models::Acl> resultAcls2;
      EXPECT_NO_THROW(resultAcls1 = directoryClient1.GetAccessControlList().Value.Acls);
      EXPECT_NO_THROW(resultAcls2 = directoryClient2.GetAccessControlList().Value.Acls);
      for (const auto& acl : resultAcls2)
      {
        auto iter = std::find_if(
            resultAcls1.begin(),
            resultAcls1.end(),
            [&acl](const Files::DataLake::Models::Acl& targetAcl) {
              return (targetAcl.Type == acl.Type) && (targetAcl.Id == acl.Id)
                  && (targetAcl.Scope == acl.Scope);
            });
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ(iter->Permissions, acl.Permissions);
      }
      {
        // verify group policy has been removed.
        auto userFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "user" && targetAcl.Id == "72a3f86f-271f-439e-b031-25678907d381";
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), userFinder);
        EXPECT_EQ(iter, resultAcls1.end());
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), userFinder);
        EXPECT_EQ(iter, resultAcls2.end());
      }
      {
        // verify other has not changed
        {
          auto otherFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
            return targetAcl.Type == "other";
          };
          auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), otherFinder);
          EXPECT_NE(iter, resultAcls1.end());
          EXPECT_EQ(originalAcls[3].Permissions, iter->Permissions);
          iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), otherFinder);
          EXPECT_NE(iter, resultAcls2.end());
          EXPECT_EQ(originalAcls[3].Permissions, iter->Permissions);
        }
        {
          auto userFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
            return targetAcl.Type == "user";
          };
          auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), userFinder);
          EXPECT_NE(iter, resultAcls1.end());
          EXPECT_EQ(originalAcls[1].Id, iter->Id);
          EXPECT_EQ(originalAcls[1].Permissions, iter->Permissions);
          iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), userFinder);
          EXPECT_NE(iter, resultAcls2.end());
          EXPECT_EQ(originalAcls[1].Id, iter->Id);
          EXPECT_EQ(originalAcls[1].Permissions, iter->Permissions);
        }
      }
    }
    {
      // Set Acls recursive, with new set of acls
      std::vector<Files::DataLake::Models::Acl> acls;
      {
        Files::DataLake::Models::Acl newAcl;
        newAcl.Type = "user";
        newAcl.Permissions = "rw-";
        acls.emplace_back(std::move(newAcl));
      }
      {
        Files::DataLake::Models::Acl newAcl;
        newAcl.Type = "group";
        newAcl.Permissions = "rw-";
        acls.emplace_back(std::move(newAcl));
      }
      {
        Files::DataLake::Models::Acl newAcl;
        newAcl.Type = "other";
        newAcl.Permissions = "rw-";
        acls.emplace_back(std::move(newAcl));
      }
      (rootDirectoryClient.SetAccessControlListRecursive(acls));
      std::vector<Files::DataLake::Models::Acl> resultAcls1;
      std::vector<Files::DataLake::Models::Acl> resultAcls2;
      EXPECT_NO_THROW(resultAcls1 = directoryClient1.GetAccessControlList().Value.Acls);
      EXPECT_NO_THROW(resultAcls2 = directoryClient2.GetAccessControlList().Value.Acls);
      for (const auto& acl : resultAcls2)
      {
        auto iter = std::find_if(
            resultAcls1.begin(),
            resultAcls1.end(),
            [&acl](const Files::DataLake::Models::Acl& targetAcl) {
              return (targetAcl.Type == acl.Type) && (targetAcl.Id == acl.Id)
                  && (targetAcl.Scope == acl.Scope);
            });
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ(iter->Permissions, acl.Permissions);
      }
      {
        // verify group has changed
        auto groupFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "group";
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), groupFinder);
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), groupFinder);
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
      }
      {
        // verify other has changed
        auto otherFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "other";
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), otherFinder);
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), otherFinder);
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
      }
      {
        // verify user has only one entry
        std::vector<Files::DataLake::Models::Acl> originalAcls = GetValidAcls();
        auto userFinder = [&originalAcls](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "user" && targetAcl.Id == originalAcls[0].Id;
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), userFinder);
        EXPECT_EQ(iter, resultAcls1.end());
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), userFinder);
        EXPECT_EQ(iter, resultAcls2.end());
      }
      {
        // verify user has changed
        auto userFinder = [](const Files::DataLake::Models::Acl& targetAcl) {
          return targetAcl.Type == "user";
        };
        auto iter = std::find_if(resultAcls1.begin(), resultAcls1.end(), userFinder);
        EXPECT_NE(iter, resultAcls1.end());
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
        iter = std::find_if(resultAcls2.begin(), resultAcls2.end(), userFinder);
        EXPECT_EQ("rw-", iter->Permissions);
        EXPECT_EQ("", iter->Id);
      }
    }
  }

  TEST_F(DataLakeDirectoryClientTest, ConstructorsWorks)
  {
    {
      // Create from connection string validates static creator function and shared key
      // constructor.
      auto directoryName = GetTestName() + "1";
      auto connectionStringClient
          = Azure::Storage::Files::DataLake::DataLakeDirectoryClient::CreateFromConnectionString(
              AdlsGen2ConnectionString(),
              m_fileSystemName,
              directoryName,
              InitClientOptions<Azure::Storage::Files::DataLake::DataLakeClientOptions>());
      EXPECT_NO_THROW(connectionStringClient.Create());
      EXPECT_NO_THROW(connectionStringClient.DeleteRecursive());
    }

    {
      // Create from client secret credential.
      std::shared_ptr<Azure::Core::Credentials::TokenCredential> credential
          = std::make_shared<Azure::Identity::ClientSecretCredential>(
              AadTenantId(), AadClientId(), AadClientSecret());

      Azure::Storage::Files::DataLake::DataLakeClientOptions options;

      auto clientSecretClient = InitTestClient<
          Azure::Storage::Files::DataLake::DataLakeDirectoryClient,
          Azure::Storage::Files::DataLake::DataLakeClientOptions>(
          Azure::Storage::Files::DataLake::_detail::GetDfsUrlFromUrl(
              Azure::Storage::Files::DataLake::DataLakeDirectoryClient::CreateFromConnectionString(
                  AdlsGen2ConnectionString(), m_fileSystemName, GetTestName() + "a")
                  .GetUrl()),
          credential,
          options);

      EXPECT_NO_THROW(clientSecretClient->Create());
      EXPECT_NO_THROW(clientSecretClient->DeleteRecursive());
    }

    {
      // Create from Anonymous credential.
      auto objectName = GetTestName() + "2";
      auto containerClient = Azure::Storage::Blobs::BlobContainerClient::CreateFromConnectionString(
          AdlsGen2ConnectionString(),
          m_fileSystemName,
          InitClientOptions<Azure::Storage::Blobs::BlobClientOptions>());
      Azure::Storage::Blobs::SetBlobContainerAccessPolicyOptions options;
      options.AccessType = Azure::Storage::Blobs::Models::PublicAccessType::BlobContainer;
      containerClient.SetAccessPolicy(options);

      auto directoryClient
          = Azure::Storage::Files::DataLake::DataLakeDirectoryClient::CreateFromConnectionString(
              AdlsGen2ConnectionString(),
              m_fileSystemName,
              objectName,
              InitClientOptions<Azure::Storage::Files::DataLake::DataLakeClientOptions>());
      EXPECT_NO_THROW(directoryClient.Create());

      auto anonymousClient = Azure::Storage::Files::DataLake::DataLakeDirectoryClient(
          directoryClient.GetUrl(),
          InitClientOptions<Azure::Storage::Files::DataLake::DataLakeClientOptions>());

      TestSleep(std::chrono::seconds(30));

      EXPECT_NO_THROW(anonymousClient.GetProperties());
    }
  }
}}} // namespace Azure::Storage::Test
