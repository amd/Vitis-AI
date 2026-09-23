
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from lxml import etree
import os
import sys

def parseXMLFile(dirName, goldFile):
    txt_file = open(goldFile, "w+")
    files = sorted(os.listdir(dirName))
    labelList = list()
    lines = list()
    for fileName in files:
        file = dirName+"/"+fileName
        xml = etree.parse(file)
        name = xml.xpath("/annotation/filename")[0].text
        labels = []
        imgSize = [list(), list()]
        boxes = [list(), list(), list(), list()]
        for i in range(len(xml.xpath("/annotation/object/name"))):
            labels.append(xml.xpath("/annotation/object/name")[i].text)
            if (xml.xpath("/annotation/object/name")[i].text) not in labelList:
                labelList.append(xml.xpath("/annotation/object/name")[i].text)
            imgSize[0].append(float(xml.xpath("/annotation/size/width")[0].text))
            imgSize[1].append(float(xml.xpath("/annotation/size/height")[0].text))
            boxes[0].append(float(xml.xpath("/annotation/object/bndbox/xmin")[i].text))
            boxes[1].append(float(xml.xpath("/annotation/object/bndbox/ymin")[i].text))
            boxes[2].append(float(xml.xpath("/annotation/object/bndbox/xmax")[i].text)-boxes[0][i])
            boxes[3].append(float(xml.xpath("/annotation/object/bndbox/ymax")[i].text)-boxes[1][i])

        for j in range(len(labels)):
            lines.append(name + "\t" + str(boxes[0][j]) + "\t" + str(boxes[1][j]) + "\t" + str(boxes[2][j]) + "\t" + str(boxes[3][j]) + "\t" + str(imgSize[0][j]) + "\t" + str(imgSize[1][j]) + "\t" + str(labelList.index(labels[j]) + 1) + "\n")

    label_list = "|"
    for k in range(len(labelList)):
        label_list = label_list + labelList[k] + ";" + str((k+1)) + "|"
    label_list = label_list + "\n"
    txt_file.write(label_list)

    for line in lines:
        txt_file.write(line)

    txt_file.close()

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print( "This script needs two argument; the pascal voc annotations folder and the gold file name.")
        exit(1)
    annotations = sys.argv[1]
    goldFile = sys.argv[2]
    parseXMLFile(annotations, goldFile)
