
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import os.path
from os import system
from sys import stdout,argv
from urllib.request import urlopen, Request, urlretrieve
import signal
import multiprocessing
import imghdr
import shutil

MAX_IMAGES = 3000

pic_list   = "links/pictures_urls.txt"
data_dir = "data"
if len(argv) > 1:
    data_dir = argv[1]
output_dir = os.path.join(data_dir,"ILSVRC2012_img_val")
gold_file  = os.path.join(data_dir,"ILSVRC_2012_val_GroundTruth_10p.dat")
gold_map   = os.path.join(data_dir,"ILSVRC_2012_val_GroundTruth_10p.txt")
if os.path.isdir(output_dir):
  system('rm -rf '+output_dir+'/*')
if not os.path.isdir(output_dir):
  os.makedirs(output_dir)


if(not os.path.isfile(pic_list)):
  print ("ERROR : path '%s' is not valid" % pic_list)
  exit()

def print_progress(cur, tot):
  stdout.write('\r\r\r\r{0:3d}%'.format(int(round(100* float(cur) / tot))))
  stdout.flush()
def init_child(lock_,counter_,goldIndex_):
  global lock
  global counter
  global goldIndex
  lock      = lock_
  counter   = counter_
  goldIndex = goldIndex_

def downloadCategory(catUrl):
  cat = catUrl[0]
  url = catUrl[1]
  global counter
  global lock
  global goldIndex

  p = multiprocessing.current_process()
  tmpFilePath = os.path.join(output_dir, "img_" + str(p._identity[0]) + "_" + str(os.getpid()))

  # Check if image is available
  try:
    with urlopen(url, timeout = 4) as response, open(tmpFilePath, 'wb') as out_file:
        shutil.copyfileobj(response, out_file)
  except Exception as e:
#    print(e)
#    print ("Could not download picture from: ",url)
    return 1
  if(os.path.exists(tmpFilePath)):
    with lock:
      file_name = 'ILSVRC2012_val_%08d.JPEG' % (counter.value + 1)
      shutil.move(tmpFilePath, os.path.join(output_dir,file_name))

      if(imghdr.what(os.path.join(output_dir,file_name))=="jpeg"):
        goldIndex[counter.value] = int(cat)
        counter.value += 1
        print_progress(counter.value, MAX_IMAGES)
      else:
#        print ("Corrupted image!!")
        system('rm -f '+ os.path.join(output_dir,file_name))
#        print ("Removed",os.path.join(output_dir,file_name))
        return 1
  return 0


if __name__ == '__main__':
  with open(pic_list,'r', encoding='utf-8') as pic_list:
    pic_array = pic_list.readlines()
    lock = multiprocessing.Lock()
    counter = multiprocessing.Value('i',0)
    goldArray = multiprocessing.Array('i',[0] * MAX_IMAGES)
    original_sigint_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    pool = multiprocessing.Pool(100,initializer=init_child,initargs=(lock,counter,goldArray))
    signal.signal(signal.SIGINT,original_sigint_handler )
    catUrl = []
    for line in pic_array :
      if line and line != "\n":
          catUrl.append([line.split()[0],line.split()[1]])
    try:
      start, stop = 0, 0
      print ('download images in progress:')
      print ('in directory : ' + output_dir)
      while counter.value < MAX_IMAGES and stop < len(catUrl):
        start += stop-start
        stop += MAX_IMAGES-counter.value
        pool.map_async(downloadCategory,catUrl[start:stop]).get(999999)
    except KeyboardInterrupt:
      print ("\nKeyboardInterrupt: %d pictures were downloaded" % counter.value)
      pool.terminate()
      with open(gold_file, "w") as gold_file:
        zeroRemoved = False
        for cat in goldArray[0:counter.value]:
          gold_file.write(str(cat+1)+'\n')
        gold_file.close()
    else:
      pool.close()
    pool.join()
    with open(gold_file, "w") as gold_file:
      zeroRemoved = False
      for cat in goldArray[0:counter.value]:
        gold_file.write(str(cat+1)+'\n')
      gold_file.close()
    with open("links/ILSVRC2012_synset_words.txt", 'r') as synset:
      synset_words_array = synset.readlines()
    with open(gold_map, "w") as gold_file:
      for i in range(counter.value):
        file_name = 'ILSVRC2012_val_%08d.JPEG' % (i+1)
        gold_file.write(file_name + " " + synset_words_array[goldArray[i]])


    print ("\n%d pictures were downloaded" % counter.value)
#    print "Gen binary file..."
#    system("BINGEN/bingen "+str(counter.value-1))
