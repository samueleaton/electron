process = global.process
ipc = require 'ipc'
v8Util = process.atomBinding 'v8_util'
CallbacksRegistry = require 'callbacks-registry'

callbacksRegistry = new CallbacksRegistry

# Convert the arguments object into an array of meta data.
wrapArgs = (args) ->
  valueToMeta = (value) ->
    if Array.isArray value
      type: 'array', value: wrapArgs(value)
    else if value? and typeof value is 'object' and v8Util.getHiddenValue value, 'atomId'
      type: 'remote-object', id: v8Util.getHiddenValue value, 'atomId'
    else if value? and typeof value is 'object'
      ret = type: 'object', name: value.constructor.name, members: []
      ret.members.push(name: prop, value: valueToMeta(field)) for prop, field of value
      ret
    else if typeof value is 'function' and v8Util.getHiddenValue value, 'returnValue'
      type: 'function-with-return-value', value: valueToMeta(value())
    else if typeof value is 'function'
      type: 'function', id: callbacksRegistry.add(value)
    else
      type: 'value', value: value

  Array::slice.call(args).map valueToMeta

# Convert meta data from browser into real value.
metaToValue = (meta, buffers) ->
  switch meta.type
    when 'value' then meta.value
    when 'array' then (metaToValue(el) for el in meta.members)
    when 'buffer' then buffers[meta.index]
    when 'error'
      throw new Error("#{meta.message}\n#{meta.stack}")
    else
      if meta.type is 'function'
        # A shadow class to represent the remote function object.
        ret =
        class RemoteFunction
          constructor: ->
            if @constructor == RemoteFunction
              # Constructor call.
              ret = ipc.sendSyncFull 'ATOM_BROWSER_CONSTRUCTOR', meta.id, wrapArgs(arguments)

              # Returning object in constructor will replace constructed object
              # with the returned object.
              # http://stackoverflow.com/questions/1978049/what-values-can-a-constructor-return-to-avoid-returning-this
              return retToValue ret
            else
              # Function call.
              ret = ipc.sendSyncFull 'ATOM_BROWSER_FUNCTION_CALL', meta.id, wrapArgs(arguments)
              return retToValue ret
      else
        ret = v8Util.createObjectWithName meta.name

      # Polulate delegate members.
      for member in meta.members
        do (member) ->
          if member.type is 'function'
            ret[member.name] =
            class RemoteMemberFunction
              constructor: ->
                if @constructor is RemoteMemberFunction
                  # Constructor call.
                  ret = ipc.sendSyncFull 'ATOM_BROWSER_MEMBER_CONSTRUCTOR', meta.id, member.name, wrapArgs(arguments)
                  return retToValue ret
                else
                  # Call member function.
                  ret = ipc.sendSyncFull 'ATOM_BROWSER_MEMBER_CALL', meta.id, member.name, wrapArgs(arguments)
                  return retToValue ret
          else
            Object.defineProperty ret, member.name,
              enumerable: true,
              configurable: false,
              set: (value) ->
                # Set member data.
                ipc.sendSyncFull 'ATOM_BROWSER_MEMBER_SET', meta.id, member.name, value
                value

              get: ->
                # Get member data.
                ret = ipc.sendSyncFull 'ATOM_BROWSER_MEMBER_GET', meta.id, member.name
                retToValue ret

      # Track delegate object's life time, and tell the browser to clean up
      # when the object is GCed.
      v8Util.setDestructor ret, ->
        ipc.send 'ATOM_BROWSER_DEREFERENCE', meta.storeId

      # Remember object's id.
      v8Util.setHiddenValue ret, 'atomId', meta.id

      ret

# Convert return value of sendSyncFull to real value.
retToValue = (ret) ->
  metaToValue ret.value, ret.buffers

# Browser calls a callback in renderer.
ipc.on 'ATOM_RENDERER_CALLBACK', (id, args) ->
  callbacksRegistry.apply id, metaToValue(args)

# A callback in browser is released.
ipc.on 'ATOM_RENDERER_RELEASE_CALLBACK', (id) ->
  callbacksRegistry.remove id

# Get remote module.
# (Just like node's require, the modules are cached permanently, note that this
#  is safe leak since the object is not expected to get freed in browser)
moduleCache = {}
exports.require = (module) ->
  return moduleCache[module] if moduleCache[module]?

  ret = ipc.sendSyncFull 'ATOM_BROWSER_REQUIRE', module
  moduleCache[module] = retToValue ret

# Get current BrowserWindow object.
windowCache = null
exports.getCurrentWindow = ->
  return windowCache if windowCache?
  ret = ipc.sendSyncFull 'ATOM_BROWSER_CURRENT_WINDOW', process.guestInstanceId
  windowCache = retToValue ret

# Get current WebContents object.
webContentsCache = null
exports.getCurrentWebContents = ->
  return webContentsCache if webContentsCache?
  ret = ipc.sendSyncFull 'ATOM_BROWSER_CURRENT_WEB_CONTENTS'
  webContentsCache = retToValue ret

# Get a global object in browser.
exports.getGlobal = (name) ->
  meta = ipc.sendSyncFull 'ATOM_BROWSER_GLOBAL', name
  retToValue meta

# Get the process object in browser.
processCache = null
exports.__defineGetter__ 'process', ->
  processCache = exports.getGlobal('process') unless processCache?
  processCache

# Create a funtion that will return the specifed value when called in browser.
exports.createFunctionWithReturnValue = (returnValue) ->
  func = -> returnValue
  v8Util.setHiddenValue func, 'returnValue', true
  func

# Get the guest WebContents from guestInstanceId.
exports.getGuestWebContents = (guestInstanceId) ->
  ret = ipc.sendSyncFull 'ATOM_BROWSER_GUEST_WEB_CONTENTS', guestInstanceId
  retToValue ret
