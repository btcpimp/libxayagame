// Copyright (C) 2018 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "defaultmain.hpp"
#include "gamelogic.hpp"
#include "storage.hpp"

#include <json/json.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <string>
#include <vector>

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya Core's JSON-RPC interface is available");
DEFINE_int32 (game_rpc_port, 0,
              "The port at which the game daemon's JSON-RPC server will be"
              " start (if non-zero)");

DEFINE_int32 (enable_pruning, -1,
              "If non-negative (including zero), enable pruning of old undo"
              " data and keep as many blocks as specified by the value");

/* This file provides a link-able main() function that calls DefaultMain.
   The game-specific stuff (game-state processing as well as name/id/version)
   needs to be defined by providing definitions for the declared extern-C
   functions in this file.

   This allows it to build games (easily) in a language other than C++
   as long as it has C-interoperability so that those C-callable functions
   can be implemented.  By linking in cmain, there is no need to write any
   C++ code at all and/or to wrap any other parts of libxayagame as long
   as basic functionality and default configuration are fine.  */

extern "C"
{

/**
 * Returns the game ID, game name and version (as string) in the provided
 * buffers.  The data is stored as a nul-terminated C string.
 *
 * The function should return zero if the operation was successful.  If the
 * provided buffer size is not large enough, it should return the minimum
 * required buffer size instead.
 */
int XayaGameGetNames (int bufferSize, char* id, char* name, char* version);

/**
 * Returns the initial game state and the associated block for games
 * on the given chain (see gamelogic.hpp for integer values).
 *
 * The buffer for hashHex is large enough to hold 64 hex digits plus
 * an optional nul terminator.
 *
 * The function should return zero on success and the minimum required
 * buffer size for the game state if the provided buffer was too small.
 */
int XayaGameGetInitialState (int chain,
                             int bufferSize,
                             char* gameState, int* gameStateSize,
                             int* height, char* hashHex);

/**
 * Processes the game state forward in time for the given moves (JSON
 * serialised to a C string).
 *
 * The buffers for the returned new game state and undo data have at least
 * the given size.  The function returns zero on success and a minimum required
 * size for both buffers if they were too small.
 */
int XayaGameProcessForward (int chain,
                            const char* oldState, int oldStateSize,
                            const char* blockData,
                            int bufferSize,
                            char* newState, int* newStateSize,
                            char* undoData, int* undoDataSize);

/**
 * Processes the game state backwards in time (undoes the given moves).
 *
 * Should return zero on success and the minimum required size for the
 * old game state if the provided buffer is too small.
 */
int XayaGameProcessBackwards (int chain,
                              const char* newState, int newStateSize,
                              const char* blockData,
                              const char* undoData, int undoDataSize,
                              int bufferSize,
                              char* oldState, int* oldStateSize);

} // extern C

namespace xaya
{
namespace
{

/**
 * A simple GameLogic implementation that calls the functions declared above
 * for the actual processing.
 */
class ExternGameLogic : public GameLogic
{

private:

  /**
   * The size that should be used for undo / game-state buffers.  To avoid
   * repeatedly passing too small buffers and having the called function
   * process the game state multiple times, we keep track of the desired size
   * over time and always just increase it (and then by a factor of two minimum)
   * when necessary.  This means that the number of failed calls will be small.
   */
  int bufferSize = 1024;

  /**
   * StreamWriterBuilder with the settings we use to serialise the JSON
   * block data for the external calls.
   */
  Json::StreamWriterBuilder jsonWriterBuilder;

  /**
   * Increases the bufferSize to the given minimum, but at least doubles it.
   */
  void
  IncreaseBufferSize (const int desiredSize)
  {
    bufferSize = std::max (desiredSize, 2 * bufferSize);
  }

public:

  ExternGameLogic ();

  GameStateData GetInitialState (unsigned& height,
                                 std::string& hashHex) override;

  GameStateData ProcessForward (const GameStateData& oldState,
                                const Json::Value& blockData,
                                UndoData& undoData) override;
  GameStateData ProcessBackwards (const GameStateData& newState,
                                  const Json::Value& blockData,
                                  const UndoData& undoData) override;

  /**
   * Calls XayaGameGetNames to find the game ID, name and version.
   */
  void GetNames (std::string& id, std::string& name, std::string& v);

};

ExternGameLogic::ExternGameLogic ()
{
  /* We don't need pretty JSON, make it as compact as possible.  */
  jsonWriterBuilder["commentStyle"] = "None";
  jsonWriterBuilder["enableYAMLCompatibility"] = false;
  jsonWriterBuilder["indentation"] = "";
}

GameStateData
ExternGameLogic::GetInitialState (unsigned& height, std::string& hashHex)
{
  int intHeight;
  int gameStateSize;
  hashHex.resize (65);
  GameStateData state;

  while (true)
    {
      state.resize (bufferSize);

      const int res
          = XayaGameGetInitialState (static_cast<int> (GetChain ()),
                                     bufferSize, &state[0], &gameStateSize,
                                     &intHeight, &hashHex[0]);
      if (res == 0)
        break;

      IncreaseBufferSize (res);
    }

  CHECK (intHeight >= 0);
  height = static_cast<unsigned> (intHeight);

  /* Get rid of a potential nul terminator.  */
  hashHex.resize (64);

  state.resize (gameStateSize);
  return state;
}

GameStateData
ExternGameLogic::ProcessForward (const GameStateData& oldState,
                                 const Json::Value& blockData,
                                 UndoData& undoData)
{
  const std::string blockDataStr
      = Json::writeString (jsonWriterBuilder, blockData);

  int newStateSize;
  int undoDataSize;
  GameStateData newState;

  while (true)
    {
      newState.resize (bufferSize);
      undoData.resize (bufferSize);

      const int res
          = XayaGameProcessForward (static_cast<int> (GetChain ()),
                                    oldState.data (), oldState.size (),
                                    blockDataStr.c_str (),
                                    bufferSize,
                                    &newState[0], &newStateSize,
                                    &undoData[0], &undoDataSize);
      if (res == 0)
        break;

      IncreaseBufferSize (res);
    }

  newState.resize (newStateSize);
  undoData.resize (undoDataSize);

  return newState;
}

GameStateData
ExternGameLogic::ProcessBackwards (const GameStateData& newState,
                                   const Json::Value& blockData,
                                   const UndoData& undoData)
{
  const std::string blockDataStr
      = Json::writeString (jsonWriterBuilder, blockData);

  int oldStateSize;
  GameStateData oldState;

  while (true)
    {
      oldState.resize (bufferSize);

      const int res
          = XayaGameProcessBackwards (static_cast<int> (GetChain ()),
                                      newState.data (), newState.size (),
                                      blockDataStr.c_str (),
                                      undoData.data (), undoData.size (),
                                      bufferSize,
                                      &oldState[0], &oldStateSize);
      if (res == 0)
        break;

      IncreaseBufferSize (res);
    }

  oldState.resize (oldStateSize);
  return newState;
}

void
ExternGameLogic::GetNames (std::string& id, std::string& name, std::string& v)
{
  std::vector<char> vid;
  std::vector<char> vname;
  std::vector<char> vversion;

  while (true)
    {
      vid.resize (bufferSize);
      vname.resize (bufferSize);
      vversion.resize (bufferSize);

      const int res
          = XayaGameGetNames (bufferSize, vid.data (), vname.data (),
                              vversion.data ());
      if (res == 0)
        break;

      IncreaseBufferSize (res);
    }

  id = std::string (vid.data ());
  name = std::string (vname.data ());
  v = std::string (vversion.data ());
}

/* TODO: Move ExternGameLogic and the function declarations to a separate
   header/source file pair, and write a simple unit test (mostly for the buffer
   resizing logic).  */

} // anonymous namespace
} // namespace xaya

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  xaya::ExternGameLogic rules;

  std::string gameId;
  std::string gameName;
  std::string gameVersion;
  rules.GetNames (gameId, gameName, gameVersion);

  gflags::SetUsageMessage ("Run " + gameName + " game daemon");
  gflags::SetVersionString (gameVersion);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  xaya::GameDaemonConfiguration config;
  config.XayaRpcUrl = FLAGS_xaya_rpc_url;
  config.GameRpcPort = FLAGS_game_rpc_port;
  config.EnablePruning = FLAGS_enable_pruning;

  return xaya::DefaultMain (config, gameId, rules);
}
