CMAKE_MINIMUM_REQUIRED( VERSION 3.3 )

PROJECT(${CRYPTO_LIB_NAME})

SET (SOURCES
   ArmoryDB/cppForSwig/Accounts.cpp
   ArmoryDB/cppForSwig/Addresses.cpp
   ArmoryDB/cppForSwig/Assets.cpp
   ArmoryDB/cppForSwig/AssetEncryption.cpp
   ArmoryDB/cppForSwig/AsyncClient.cpp
   ArmoryDB/cppForSwig/AuthorizedPeers.cpp
   ArmoryDB/cppForSwig/BinaryData.cpp
   ArmoryDB/cppForSwig/BIP150_151.cpp
   ArmoryDB/cppForSwig/BIP32_Node.cpp
   ArmoryDB/cppForSwig/BlockDataManagerConfig.cpp
   ArmoryDB/cppForSwig/BtcUtils.cpp
   ArmoryDB/cppForSwig/ClientClasses.cpp
   ArmoryDB/cppForSwig/CoinSelection.cpp
   ArmoryDB/cppForSwig/DBUtils.cpp
   ArmoryDB/cppForSwig/DecryptedDataContainer.cpp
   ArmoryDB/cppForSwig/DerivationScheme.cpp
   ArmoryDB/cppForSwig/EncryptionUtils_libbtc.cpp
   ArmoryDB/cppForSwig/hkdf.cpp
   ArmoryDB/cppForSwig/HttpMessage.cpp
   ArmoryDB/cppForSwig/JSON_codec.cpp
   ArmoryDB/cppForSwig/KDF.cpp
   ArmoryDB/cppForSwig/lmdb_wrapper.cpp
   ArmoryDB/cppForSwig/log.cpp
   ArmoryDB/cppForSwig/NetworkConfig.cpp
   ArmoryDB/cppForSwig/ReentrantLock.cpp
   ArmoryDB/cppForSwig/Script.cpp
   ArmoryDB/cppForSwig/ScriptRecipient.cpp
   ArmoryDB/cppForSwig/SecureBinaryData.cpp
   ArmoryDB/cppForSwig/Signer.cpp
   ArmoryDB/cppForSwig/SocketObject.cpp
   ArmoryDB/cppForSwig/SwigClient.cpp
   ArmoryDB/cppForSwig/StringSockets.cpp
   ArmoryDB/cppForSwig/Transactions.cpp
   ArmoryDB/cppForSwig/TxClasses.cpp
   ArmoryDB/cppForSwig/TxEvalState.cpp
   ArmoryDB/cppForSwig/UniversalTimer.cpp
   ArmoryDB/cppForSwig/Wallets.cpp
   ArmoryDB/cppForSwig/WalletManager.cpp
   ArmoryDB/cppForSwig/WebSocketClient.cpp
   ArmoryDB/cppForSwig/WebSocketMessage.cpp
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/mdb.c
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/midl.c
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/lmdbpp.cpp
   ArmoryDB/cppForSwig/bech32/ref/c++/bech32.cpp
   ArmoryDB/cppForSwig/bech32/ref/c++/segwit_addr.cpp
   ArmoryDB/cppForSwig/chacha20poly1305/chacha.c
   ArmoryDB/cppForSwig/chacha20poly1305/chachapoly_aead.c
   ArmoryDB/cppForSwig/chacha20poly1305/poly1305.c
   )

IF (BUILD_TEST_TOOLS)
   LIST (APPEND SOURCES
      ArmoryDB/cppForSwig/BDM_mainthread.cpp
      ArmoryDB/cppForSwig/BDM_Server.cpp
      ArmoryDB/cppForSwig/BitcoinP2P.cpp
      ArmoryDB/cppForSwig/Blockchain.cpp
      ArmoryDB/cppForSwig/BlockchainScanner.cpp
      ArmoryDB/cppForSwig/BlockchainScanner_Super.cpp
      ArmoryDB/cppForSwig/BlockDataMap.cpp
      ArmoryDB/cppForSwig/BlockDataViewer.cpp
      ArmoryDB/cppForSwig/BlockObj.cpp
      ArmoryDB/cppForSwig/BlockUtils.cpp
      ArmoryDB/cppForSwig/BtcWallet.cpp
      ArmoryDB/cppForSwig/DatabaseBuilder.cpp
      ArmoryDB/cppForSwig/HistoryPager.cpp
      ArmoryDB/cppForSwig/LedgerEntry.cpp
      ArmoryDB/cppForSwig/nodeRPC.cpp
      ArmoryDB/cppForSwig/Progress.cpp
      ArmoryDB/cppForSwig/ScrAddrFilter.cpp
      ArmoryDB/cppForSwig/ScrAddrObj.cpp
      ArmoryDB/cppForSwig/Server.cpp
      ArmoryDB/cppForSwig/SshParser.cpp
      ArmoryDB/cppForSwig/StoredBlockObj.cpp
      ArmoryDB/cppForSwig/txio.cpp
      ArmoryDB/cppForSwig/ZeroConf.cpp
      ArmoryDB/cppForSwig/gtest/NodeUnitTest.cpp
   )

ENDIF (BUILD_TEST_TOOLS)

IF (WIN32)
   LIST(APPEND SOURCES ArmoryDB/cppForSwig/SocketService_win32.cpp)
ELSE (WIN32)
   LIST(APPEND SOURCES ArmoryDB/cppForSwig/SocketService_unix.cpp)
ENDIF (WIN32)

SET (HEADERS
   ArmoryDB/cppForSwig/Accounts.h
   ArmoryDB/cppForSwig/Addresses.h
   ArmoryDB/cppForSwig/Assets.h
   ArmoryDB/cppForSwig/AssetEncryption.h
   ArmoryDB/cppForSwig/AsyncClient.h
   ArmoryDB/cppForSwig/AuthorizedPeers.h
   ArmoryDB/cppForSwig/BDVCodec.h
   ArmoryDB/cppForSwig/BinaryData.h
   ArmoryDB/cppForSwig/BIP150_151.h
   ArmoryDB/cppForSwig/BIP32_Node.h
   ArmoryDB/cppForSwig/BlockDataManagerConfig.h
   ArmoryDB/cppForSwig/BtcUtils.h
   ArmoryDB/cppForSwig/ClientClasses.h
   ArmoryDB/cppForSwig/CoinSelection.h
   ArmoryDB/cppForSwig/DBUtils.h
   ArmoryDB/cppForSwig/DecryptedDataContainer.h
   ArmoryDB/cppForSwig/DerivationScheme.h
   ArmoryDB/cppForSwig/EncryptionUtils.h
   ArmoryDB/cppForSwig/hkdf.h
   ArmoryDB/cppForSwig/HttpMessage.h
   ArmoryDB/cppForSwig/JSON_codec.h
   ArmoryDB/cppForSwig/lmdb_wrapper.h
   ArmoryDB/cppForSwig/NetworkConfig.h
   ArmoryDB/cppForSwig/ReentrantLock.h
   ArmoryDB/cppForSwig/SecureBinaryData.h
   ArmoryDB/cppForSwig/Signer.h
   ArmoryDB/cppForSwig/Script.h
   ArmoryDB/cppForSwig/SocketObject.h
   ArmoryDB/cppForSwig/SocketService.h
   ArmoryDB/cppForSwig/StringSockets.h
   ArmoryDB/cppForSwig/SwigClient.h
   ArmoryDB/cppForSwig/Transactions.h
   ArmoryDB/cppForSwig/TxClasses.h
   ArmoryDB/cppForSwig/TxEvalState.h
   ArmoryDB/cppForSwig/Wallets.h
   ArmoryDB/cppForSwig/WalletManager.h
   ArmoryDB/cppForSwig/WebSocketClient.h
   ArmoryDB/cppForSwig/WebSocketMessage.h
   ArmoryDB/cppForSwig/UniversalTimer.h
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/lmdb.h
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/midl.h
   ArmoryDB/cppForSwig/lmdb/libraries/liblmdb/lmdbpp.h
   ArmoryDB/cppForSwig/bech32/ref/c++/bech32.h
   ArmoryDB/cppForSwig/bech32/ref/c++/segwit_addr.h
   )

IF (BUILD_TEST_TOOLS)
   LIST (APPEND HEADERS
      ArmoryDB/cppForSwig/BDM_mainthread.h
      ArmoryDB/cppForSwig/BDM_Server.h
      ArmoryDB/cppForSwig/BitcoinP2p.h
      ArmoryDB/cppForSwig/Blockchain.h
      ArmoryDB/cppForSwig/BlockchainScanner.h
      ArmoryDB/cppForSwig/BlockchainScanner_Super.h
      ArmoryDB/cppForSwig/BlockDataMap.h
      ArmoryDB/cppForSwig/BlockDataViewer.h
      ArmoryDB/cppForSwig/BlockObj.h
      ArmoryDB/cppForSwig/BlockUtils.h
      ArmoryDB/cppForSwig/BtcWallet.h
      ArmoryDB/cppForSwig/DatabaseBuilder.h
      ArmoryDB/cppForSwig/HistoryPager.h
      ArmoryDB/cppForSwig/LedgerEntry.h
      ArmoryDB/cppForSwig/nodeRPC.h
      ArmoryDB/cppForSwig/Progress.h
      ArmoryDB/cppForSwig/ScrAddrFilter.h
      ArmoryDB/cppForSwig/ScrAddrObj.h
      ArmoryDB/cppForSwig/Server.h
      ArmoryDB/cppForSwig/SshParser.h
      ArmoryDB/cppForSwig/StoredBlockObj.h
      ArmoryDB/cppForSwig/txio.h
      ArmoryDB/cppForSwig/ZeroConf.h
      ArmoryDB/cppForSwig/gtest/NodeUnitTest.h
   )
ENDIF (BUILD_TEST_TOOLS)

SET(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ArmoryDB/cppForSwig/protobuf)
FILE(GLOB PROTO_FILES ${PROTO_DIR}/*.proto)

FOREACH(PROTO_FILE ${PROTO_FILES})
   GET_FILENAME_COMPONENT(PROTO_NAME "${PROTO_FILE}" NAME_WE)

   SET(PROTO_SOURCE_FILE "${PROTO_DIR}/${PROTO_NAME}.pb.cc")
   SET(PROTO_HEADER_FILE "${PROTO_DIR}/${PROTO_NAME}.pb.h")

   SET(PROTO_SOURCE_FILES ${PROTO_SOURCE_FILES} ${PROTO_SOURCE_FILE})
   SET(PROTO_HEADER_FILES ${PROTO_HEADER_FILES} ${PROTO_HEADER_FILE})

   ADD_CUSTOM_COMMAND(OUTPUT ${PROTO_SOURCE_FILE}
                      OUTPUT ${PROTO_HEADER_FILE}
                      DEPENDS ${PROTO_FILE}
                      COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FILE} --cpp_out=${PROTO_DIR} --proto_path=${PROTO_DIR}
   )
ENDFOREACH(PROTO_FILE)

SET(PROTO_SOURCES ${PROTO_SOURCE_FILES} ${PROTO_HEADER_FILES})

# leveldb_windows_port
IF(WIN32)
   SET( LEVELDB_WINDOWS_PORT_SOURCES
      ${LEVELDB_WINDOWS_PORT_DIR}/Win_TranslatePath.cpp
      ${LEVELDB_WINDOWS_PORT_DIR}/dirent_win32.cpp
      ${LEVELDB_WINDOWS_PORT_DIR}/mman.cpp
      ${LEVELDB_WINDOWS_PORT_DIR}/pthread_win32port.cpp
      ${LEVELDB_WINDOWS_PORT_DIR}/win32_posix.cc
      )


   SET(LEVELDB_WINDOWS_PORT_HEADERS
      ${LEVELDB_WINDOWS_PORT_DIR}/Win_TranslatePath.h
      ${LEVELDB_WINDOWS_PORT_DIR}/dirent_win32.h
      ${LEVELDB_WINDOWS_PORT_DIR}/file.h
      ${LEVELDB_WINDOWS_PORT_DIR}/mman.h
      ${LEVELDB_WINDOWS_PORT_DIR}/pthread.h
      ${LEVELDB_WINDOWS_PORT_DIR}/unistd.h
      ${LEVELDB_WINDOWS_PORT_DIR}/win32_posix.h
      )

   SET(OS_SPECIFIC_SOURCES ${LEVELDB_WINDOWS_PORT_SOURCES})
   SET(OS_SPECIFIC_HEADERS ${LEVELDB_WINDOWS_PORT_HEADERS})
ENDIF(WIN32)

ADD_LIBRARY(${CRYPTO_LIB_NAME}
   ${SOURCES} ${HEADERS} ${PROTO_SOURCES}
   ${CRYPTOPP_EXTRA_SOURCES} ${CRYPTOPP_EXTRA_HEADERS}
   ${OS_SPECIFIC_SOURCES} ${OS_SPECIFIC_HEADERS})

IF (BUILD_TEST_TOOLS)
   TARGET_COMPILE_DEFINITIONS( ${CRYPTO_LIB_NAME} PRIVATE
      COINBASE_MATURITY_TESTS)
ENDIF (BUILD_TEST_TOOLS)

TARGET_LINK_LIBRARIES(${CRYPTO_LIB_NAME}
   ${WS_LIB}
   ${CRYPTOPP_LIB}
   )

IF(NOT WIN32)
   SET(ARMORYDB_SOURCES)
   SET(ARMORYDB_HEADERS)
ENDIF(NOT WIN32)

TARGET_INCLUDE_DIRECTORIES(${CRYPTO_LIB_NAME}
   PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ArmoryDB/cppForSwig
   PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ArmoryDB/cppForSwig/lmdb/libraries/liblmdb
)
