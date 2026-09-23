
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from collections import deque
from threading import Thread
import cv2
import os

import numpy as np
import PIL
import math
############################################################
############################################################

# the application will open several window and place them on the desktop at
# specific location. The below variables allow to configure the location

# this is the X/Y offset for window position
WINDOW_X_OFFSET         = int(os.environ.get("WINDOW_X_OFFSET")         or 0)
WINDOW_Y_OFFSET         = int(os.environ.get("WINDOW_Y_OFFSET")         or 0)

# this is the number of window per row
WINDOW_PER_ROW          = int(os.environ.get("WINDOW_PER_ROW")          or 3)

# this is a scaling factor to reduce the window dimension (2: window will be 2x smaller)
WINDOW_RATIO            = float(os.environ.get("WINDOW_RATIO")          or 1)

# this force the width and height of the video
WINDOW_WIDTH            = int(os.environ.get("WINDOW_WIDTH")            or 0)
WINDOW_HEIGHT           = int(os.environ.get("WINDOW_HEIGHT")           or 0)

# this is the index position to place a window so a slide can be displayed. For instance, in a 3x3 configuration use 4 to let the center empty
WINDOW_SLIDE_IDX        = int(os.environ.get("WINDOW_SLIDE_IDX")        or 4)

# this is the number of batch (usually the number of cores) per system
BATCH_PER_SYSTEM        = int(os.environ.get("BATCH_PER_SYSTEM")        or 1)

# this is the number of system per board
SYSTEM_PER_BOARD        = int(os.environ.get("SYSTEM_PER_BOARD")        or 2)

# show info/FPS by default
SHOW_INFO               = int(os.environ.get("SHOW_INFO")               or 0)
SHOW_FPS                = int(os.environ.get("SHOW_FPS")                or 0)


# enable the MJPG mode
OPENCV_CAMERA_MJPG      = int(os.environ.get("OPENCV_CAMERA_MJPG")      or 1)
# force camera width/height
OPENCV_CAMERA_WIDTH     = int(os.environ.get("OPENCV_CAMERA_WIDTH")     or 0)
OPENCV_CAMERA_HEIGHT    = int(os.environ.get("OPENCV_CAMERA_HEIGHT")    or 0)


############################################################
############################################################

# only for YoloV2:
def mipso_get_interpolation():
    return cv2.INTER_NEAREST
    return cv2.INTER_LINEAR
    #return cv2.INTER_LANCZOS4

# only for YoloV2
def mipso_camera_reduce_delay():
    return False # normal execution: capture / inference / display from capture
    return True # reduce delay execution: inference on prev capture / capture / display from capture using previous inference

# only for YoloV2
def mipso_box_rounding(i):
    return i
    round = 20
    return ((i + round) // (2*round) ) * 2*round

def mipso_camera_init(c):
    print ("Initial camera resolution {}x{} @ {}".format(
        c.get(cv2.CAP_PROP_FRAME_WIDTH),
        c.get(cv2.CAP_PROP_FRAME_HEIGHT),
        c.get(cv2.CAP_PROP_FPS)))
    if OPENCV_CAMERA_WIDTH != 0:
        c.set(cv2.CAP_PROP_FRAME_WIDTH , OPENCV_CAMERA_WIDTH)
    if OPENCV_CAMERA_HEIGHT != 0:
        c.set(cv2.CAP_PROP_FRAME_HEIGHT , OPENCV_CAMERA_HEIGHT)
    if OPENCV_CAMERA_MJPG != 0:
        c.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter.fourcc('M', 'J', 'P', 'G'))
    print ("Using camera resolution {}x{} @ {}".format(
        c.get(cv2.CAP_PROP_FRAME_WIDTH),
        c.get(cv2.CAP_PROP_FRAME_HEIGHT),
        c.get(cv2.CAP_PROP_FPS)))

def mipso_window_init(idx, width, height):
    cv2.namedWindow(str(idx), cv2.WINDOW_GUI_NORMAL | cv2.WINDOW_FREERATIO)
    #cv2.namedWindow(str(idx), cv2.WINDOW_NORMAL)

    if WINDOW_WIDTH != 0 and WINDOW_HEIGHT != 0:
        cur_width = WINDOW_WIDTH
        cur_height = WINDOW_HEIGHT
    else:
        cur_width = int(width // WINDOW_RATIO)
        cur_height = int(height // WINDOW_RATIO)

    cv2.resizeWindow(str(idx), (cur_width, cur_height))
    board=0
    system=0
    core=0
    if 'VAISW_RUNSESSION_ENABLECORES' in os.environ and os.environ['VAISW_RUNSESSION_ENABLECORES'].startswith('B'):
        c = os.environ['VAISW_RUNSESSION_ENABLECORES'].split(':')[0].split('_')
        if len(c[0]) > 0:
            board = int(c[0][1:])
        if len(c) > 1:
            system = int(c[1][1:])
        if len(c) > 2:
            core = int(c[2][1:])

    cur_idx = idx + core + BATCH_PER_SYSTEM * ( system + SYSTEM_PER_BOARD * board)
    # in case we want to put slide somewhere
    if cur_idx >= WINDOW_SLIDE_IDX:
        cur_idx = cur_idx + 1
    pos_x = WINDOW_X_OFFSET + int( (cur_idx % WINDOW_PER_ROW)*cur_width)
    pos_y = WINDOW_Y_OFFSET + int( 0 + (cur_idx // WINDOW_PER_ROW)*cur_height)

    print("Move window {} {}x{} for system {} @ ({} , {})".format(idx, cur_width, cur_height, system, pos_x, pos_y))
    cv2.waitKey(1)  # this is needed for latest opencv version
    cv2.moveWindow(str(idx), pos_x, pos_y)

def mipso_parse_input(file):
    mode='file'
    for part in file.split(':'):
        if 'camera' == part:
            mode='camera'
            continue
        if part.startswith('http'):
            mode=part
            continue
        if part.isdigit():
            yield mode, int(part)
        elif mode.startswith('http'):
            yield 'http', mode + ':' + part
            mode='file'
        else:
            mode='file'
            yield mode, part


class captureDirectory(object):
    def __init__(self, path):
        self.path = path
        self.l = iter([os.path.join(self.path, x) for x in sorted(os.listdir(self.path))])

    def isOpened(self):
        return os.path.isdir(self.path)
    def release(self):
        return
    def get(self, prop):
        # only return the FPS
        return 1
    def read(self):
        try:
            i = next(self.l)
            while(os.path.isdir(i)):
              i = next(self.l)
            print("Processing file ", i)
        except StopIteration:
            return False, None
        return True, cv2.imread(i)

class liveCapture(object):
    def __init__(self,src):
        self.stop = False
        self.status = False
        self.frames = deque([])
        self.capture = cv2.VideoCapture(src)
        #self.capture.set(cv2.CAP_PROP_BUFFERSIZE,2)
        self.FPS = self.capture.get(cv2.CAP_PROP_FPS)
        self.thread1 = Thread(target = self.bg_read, args=())
        self.thread1.daemon = True
        self.thread1.start()
        self.readFps = 1
    def bg_read(self):
        while not self.stop:
            if self.capture.isOpened():
                self.status, frame = self.capture.read()
                if self.status:
                    self.frames.append(frame)
    def isOpened(self):
        return self.capture.isOpened()
    def release(self):
        return
    def read(self):
        # do a kind of adaptative fast forward with input buffer of 200 images
        l = len(self.frames)
        if l > 200:
            self.readFps = 1 << ((l//100)-1)
        elif l < 200:
            self.readFps = 1
        if len(self.frames) > 0:
            f = self.frames[0]
            for _ in range(int(self.readFps)):
                # keep always 1 frame in the buffer
                if len(self.frames) == 1:
                    break
                self.frames.popleft()
            return True, f
        else:
            return False, None
    def get(self, prop):
        # only return the FPS
        return int(self.FPS)
    def __del__(self):
        self.stop = True
        self.thread1.join()


def mipso_opencv_capture(file):
    """
    return a list of opencv capture device
    """
    camera = []
    # TODO: use a better handling of the different mode (class)
    if 'camera' == file:
        # open all camera
        camera_idx=0
        while True:
            c = cv2.VideoCapture(camera_idx)
            if c.isOpened() == False:
                break
            camera.append(c)
            mipso_camera_init(c)
            camera_idx = camera_idx + 1
    else:
        for mode, url in mipso_parse_input(file):
            if mode.startswith('http') and 'youtube' in url:
                import pafy
                option = url.split('@')
                v = pafy.new(option[0])
                resolution = [s.resolution for s in v.allstreams]
                if option[-1] == 'list':
                    print(f'Available resolution for {option[0]} are:')
                    print('\n'.join(resolution))
                    exit(0)
                elif option[-1] in resolution:
                    url = v.allstreams[resolution.index(option[-1])].url
                else:
                    url = v.getbest().url
                if 'live' in option:
                    c = liveCapture(url)
                else:
                    c = cv2.VideoCapture(url)
            elif os.path.isdir(url):
                c = captureDirectory(url)
            else:
                c = cv2.VideoCapture(url)
            if c.isOpened() == False:
                print("Error opening source {} {}".format(mode, url))
                break
            if mode == 'camera':
                mipso_camera_init(c)
            camera.append(c)

    assert len(camera) > 0, 'Cannot capture source'
    return camera

# TODO: create a Mipso class
showHelp = False
pauseMode = False
showWindowInfo = SHOW_INFO != 0
showFps = SHOW_FPS != 0

def mipso_process_key(key):
    global showHelp
    global pauseMode
    global showWindowInfo
    global showFps

    if pauseMode:
        print ("pause mode, press 'p' to play or any other key to move to the next frame")
    while True:
        if key == 27:
            return key
        if key > 0 and chr(key) in 'h':
            showHelp = not showHelp
        if key > 0 and chr(key) in 'p':
            pauseMode = not pauseMode
        if key > 0 and chr(key) in 'i':
            showWindowInfo = not showWindowInfo
        if key > 0 and chr(key) in 'f':
            showFps = not showFps
        if not pauseMode or key != -1:
            break
        key = cv2.waitKey(1)
    return key

def print_help(frame):

    HELP="\
Key usage:\n\
    ESC : exit the demo\n\
    h   : toggle displaying this help\n\
    p   : pause the video (any other key will do frame by frame)\n\
    i   : toggle showing network information\n\
    f   : toggle showing FPS\n\
"
    scale = 1
    if frame.shape[1] < 1240:
        scale = scale * frame.shape[1] / 1240
    for y, line in enumerate(HELP.split('\n')):
        cv2.putText(frame, line, (20, int(scale*(200 + y * 40))), cv2.FONT_HERSHEY_SIMPLEX,
                    scale, (255,255,0), 1)


def print_text(frame, x, y, text, scale=1.2):
    font                   = cv2.FONT_HERSHEY_SIMPLEX
    bottomLeftCornerOfText = (10,500)
    fontScale              = scale
    fontColor              = (0,0,255)
    lineType               = 2 if scale >= 1 else 1

    cv2.putText(frame, text, (x, y),
        font,
        fontScale,
        fontColor,
        lineType)

def mipso_window_info(f, idx, name, latency):
    if showHelp:
        print_help(f)
    if showWindowInfo:
        scale = 1
        if f.shape[1] < 500:
            scale = scale * f.shape[1] / 500
        if showFps:
            print_text( f, 10, int(scale*30), "Vaisw: {} FPS={}".format(name, str(int(1/latency))), scale )
        else:
            print_text( f, 10, int(scale*30), "Vaisw: {}".format(name), scale)
        #print_text(f, 10, 50, "Latency is: " + str(int(1000*latency)) + " ms. FPS is: " + str(int(1/latency)) + " img/s")

#inputSize is a tuple (x, y), meaning (width, height). Flag should respect that format
#resize_function is a caller-provided function that takes image, x and y as input and returns the resized image
#get_size_function is a caller-provided function that returns the size (x, y) of the input image to handle cases where the input image is not know
inputSize = None

def mipso_resize_input(flag_inputSize, input, resize_function, get_size_function=None, resize_method=None):
    global inputSize

    assert callable(resize_function), 'Expected a resize function as third argument'

    def getImageSize(input):
        if isinstance(input, PIL.Image.Image):
                size = input.size
        elif isinstance(input, np.ndarray):
                size = input.shape[1::-1]
        elif get_size_function is not None:
                size = get_size_function(input)
        else:
            raise TypeError(f'Unknown image type {type(input)}, cannot guess shape of image')
        return size

    # Resize LetterBox takes input image and resizes it while preserving aspect ratio
    # This is done by scaling the input image to
    # the lowest of height/width ratio then replacing the center pixels of a fully black image
    # with the scaled input
    def resize_LetterBox(input, x, y):
        iw, ih = getImageSize(input)

        if (x,y) == (iw,ih):
            return input

        scale = min(y / ih, x / iw)
        nh = int(ih * scale)
        nw = int(iw * scale)

        image = resize_function(input, nw, nh)
        new_img = np.full((y, x, 3), 0, dtype='uint8')
        new_img[(y - nh) // 2:(y - nh) // 2 + nh,
                (x - nw) // 2:(x - nw) // 2 + nw,
                :] = image.copy()
        return new_img

    # Resize Panscan takes input image and resizes it while preserving aspect ratio
    # This is done by scaling the input image to the highest of height/width ratio
    # then cropping the pixels that don't fit in the requested size
    def resize_PanScan(input, x, y):
        w, h = getImageSize(input)

        if (x,y) == (w,h):
            return input

        if h == w:
            return resize_function(input, x,y)

        new_w = max(w*x//w, w*y//h)
        new_h = max(h*x//w, h*y//h)
        out_image = resize_function(input, new_w, new_h)
        cropped_out_image= out_image[(new_h-y)//2:(new_h-y)//2+y, (new_w-x)//2:(new_w-x)//2+x,:]
        return cropped_out_image

    resizeMethods = {
                     None       : resize_function,
                     'LetterBox': resize_LetterBox,
                     'PanScan'  : resize_PanScan,
                    }

    def call_resize_function(input, x, y):
        return resizeMethods[resize_method](input, x, y)

    def parse_inputSize(inputSize_string):
        return tuple( int(dim) for dim in inputSize_string.split('x') )

    if flag_inputSize is None:
        out_image = input
    elif flag_inputSize == "auto":
        if inputSize is None:
            inputSize = getImageSize(input)
            out_image = input
        else:
            out_image = call_resize_function(input, x=inputSize[0], y=inputSize[1])
    else:
        if inputSize is None:
            inputSize = parse_inputSize(flag_inputSize)
        out_image = call_resize_function(input, x=inputSize[0], y=inputSize[1])
    return out_image


def mipso_resize_output(output, target_size, resize_function, get_size_function=None, resize_method=None):


    assert callable(resize_function), 'Expected a resize function as third argument'

    def getImageSize(input):
        if isinstance(input, PIL.Image.Image):
                size = input.size
        elif isinstance(input, np.ndarray):
                size = input.shape[1::-1]
        elif get_size_function is not None:
                size = get_size_function(input)
        else:
            raise TypeError(f'Unknown image type {type(input)}, cannot guess shape of image')
        return size

    def reverse_resize_LetterBox(output, x, y):
        out_w, out_h = getImageSize(output)
        upscale_ratio = min(out_w/x, out_h/y)
        upscaled_dims = ( math.floor(x*upscale_ratio), math.floor(y*upscale_ratio) )
        black_bars = ((out_w - upscaled_dims[0]) //2,
                      (out_h - upscaled_dims[1]) //2)

        if black_bars[0]:
            out_image = output[black_bars[0]:-black_bars[0]]
        elif black_bars[1]:
            out_image = out_image[:,black_bars[1]:-black_bars[1]]
        else:
            out_image = output

        return resize_function(out_image, x, y)

    def reverse_resize_PanScan(output, x, y):
        out_w, out_h = getImageSize(output)
        upscale_ratio = max(out_w/x, out_h/y)
        upscaled_dims = ( math.floor(x*upscale_ratio), math.floor(y*upscale_ratio) )

        # Cropped areas are HW due to being on numpy mask
        cropped_areas = ( int(round(( upscaled_dims[1] - out_h)/(2*upscale_ratio))),
                          int(round(( upscaled_dims[0] - out_w)/(2*upscale_ratio))))

        resize_shape = (int(out_w/upscale_ratio), int(out_h/upscale_ratio))
        resized_output = resize_function(output, resize_shape[0], resize_shape[1])

        black_mask = np.full( (y, x, 3), 0, dtype='uint8')
        if cropped_areas[0]:
            black_mask[cropped_areas[0]:-cropped_areas[0]] = resized_output
        elif cropped_areas[1]:
            black_mask[:,cropped_areas[1]:-cropped_areas[1]] = resized_output
        else:
            black_mask = resized_output

        return black_mask

    resizeMethods = {
                     None       : resize_function,
                     'LetterBox': reverse_resize_LetterBox,
                     'PanScan'  : reverse_resize_PanScan,
                    }

    def call_resize_function(output, x, y):
        return resizeMethods[resize_method](output, x, y)

    out_image = call_resize_function(output, target_size[0], target_size[1])

    return out_image

class MipsoSaver:
    """
    This class is used to ease porting of a consistent saving functionality on all Mipsology demos

    Class is initizalized with the following values:
    out_path       : String that represents the output base path. It is a folder that will contain the video or the images. If it doesn't exist It will be created
    as_images      : Boolean that indicates that we want to write images and not a video
    dump_interval  : Integer that controls the frequency at which images will be saved on disk to not fill memory
    fourcc         : Cv2 fourcc codec to save video
    fps            : Fps of the video file
    video_filename : Name of the video file

    Class contains the following methods:
    add            : Method that receives frames
                     in video mode, store the frames in an internal buffer
                     in as_images mode, also accepts a filename parameter and will periodically (based on dump_interval class attribute) call the save method
    save           : Method to call to save added frames to disk as video or images.
    """
    def __init__(self, out_path, as_images=False, dump_interval=100, fourcc=None, fps=30, video_filename='output.avi'):
        self.out_path = out_path
        self.as_images = as_images
        # as_images == True related options (images mode)
        self.dump_interval = dump_interval
        # as_images == False related options (video mode)
        if not self.as_images:
            self.fourcc = fourcc if fourcc else cv2.VideoWriter_fourcc(*'XVID')
        self.fps = fps
        self.video_filename = video_filename
        self.buffer = {} if self.as_images else []
        self.frame_counter = 0

        if self.out_path:
            if not os.path.exists(self.out_path):
                os.makedirs(self.out_path)

    def add(self, frame, filename=None):
        if not self.out_path or self.out_path == '/dev/null':
            return

        if self.as_images:
            if not filename:
                filename = f'frame_{self.frame_counter}.png'
                self.frame_counter += 1
            self.buffer.update({filename : frame})
            if len(self.buffer) >= self.dump_interval:
                self.save()
        else:
            self.buffer.append(frame)

    def save(self):
        if not self.out_path or self.out_path == '/dev/null':
            return

        if self.as_images:
            for filename, image in self.buffer.items():
                if isinstance(image, np.ndarray):
                    image = image if image.dtype == 'uint8' else image.astype('uint8')
                cv2.imwrite(os.path.join(self.out_path, filename), image)
        else:
            if self.buffer:
                vidwri = cv2.VideoWriter(
                            os.path.join(self.out_path,self.video_filename),
                            self.fourcc,
                            self.fps,
                            (self.buffer[0].shape[1], self.buffer[0].shape[0])
                            )
                for img in self.buffer:
                    if isinstance(img, np.ndarray):
                        img = img if img.dtype == 'uint8' else img.astype('uint8')
                    else:
                        print(f'Warning unknown type {type(img)}, saving to video might not work')
                    vidwri.write(img)
                vidwri.release()

        self.buffer.clear()
