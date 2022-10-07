import os
import random
import string
import numpy
import matplotlib.pyplot as plotter
from PIL import Image
from random import randrange
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import imageDuplicateFinder
from programConstants import constants

#TODO: Could shift this class to fileFolderOperations.py
class ImageGenerator: #https://stackoverflow.com/questions/15261851/100x100-image-with-random-pixel-colour
    def __init__(self):
        self.IMAGE_Z_AXIS = 3
        self.SHADE_RANGE_START = 0
        self.MAX_SHADE_RANGE = 255
        self.MAX_SHADE_RANGE_SANS_ZERO = 256
        self.PNG_Extension = ".png"
        self.JPG_Extension = ".jpg"
        self.WEBP_Extension = ".webp"
    
    # def generateRandomRGB_PNG_Image(self, imageWidth, imageHeight, filenameWithPath):
    #     imageArray = numpy.random.rand(imageHeight, imageWidth, self.IMAGE_Z_AXIS) * self.MAX_SHADE_RANGE
    #     generatedImage = Image.fromarray(imageArray.astype('uint8')).convert('RGBA')
    #     if not filenameWithPath.lower().endswith(self.PNG_Extension):
    #         filenameWithPath = filenameWithPath + self.PNG_Extension
    #     generatedImage.save(filenameWithPath)
    #     return filenameWithPath
        
    def convertThisImageToJPG(self, filenameWithPath):
        img = Image.open(filenameWithPath)
        img = img.convert('RGB')
        imageNameWithoutExtension = os.path.splitext(filenameWithPath)[constants.GlobalConstants.FIRST_POSITION_IN_LIST] #The path will be retained but the ".png" will be removed
        filenameWithPath = imageNameWithoutExtension + self.JPG_Extension
        img.save(filenameWithPath)
        return filenameWithPath
    
    def convertThisImageToWEBP(self, filenameWithPath):
        img = Image.open(filenameWithPath)
        img = img.convert('RGB')
        imageNameWithoutExtension = os.path.splitext(filenameWithPath)[constants.GlobalConstants.FIRST_POSITION_IN_LIST] #The path will be retained but the ".png" will be removed
        filenameWithPath = imageNameWithoutExtension + self.WEBP_Extension
        img.save(filenameWithPath, 'webp')
        return filenameWithPath
    
    def generateRandomBarGraph(self, filenameWithPath):
        randMin = 1; randMax = 100
        numberOfAxisElements = randrange(randMin, randMax)
        xAxis = [randrange(randMin, randMax) for _ in range(0, numberOfAxisElements)]
        yAxis = [random.choice(string.ascii_letters) for _ in range(0, numberOfAxisElements)]
        xAxis = numpy.array(xAxis)
        yAxis = numpy.array(yAxis)
        plotter.bar(xAxis, yAxis)
        plotter.savefig(filenameWithPath)
        #plotter.show() #show should be after savefig. Else the image saved will be blank
        
class TestImageDuplicateFinding:
    def test_searchingInEmptyFolder(self):#duplicateImagesFolder should not get created
        #---first create a dummy folder
        fileFolderOps = fileOps.FileOperations()
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + "folderImgEmpty", "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        #---run a search on empty directory 
        duplicateFinder = imageDuplicateFinder.ImageDuplicateSearch(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchWithoutMovingFile()
        duplicateFinder.search()
        assert not duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        subFolders = fileFolderOps.getListOfSubfoldersInThisFolder(folderToSearch)
        assert len(subFolders) == 0 #no duplicateImagesFolder generated
        
    def test_searchingInFolderWithDuplicateImagesOfSameSize_PNG_JPG_WEBP(self):
        numberOfDuplicatesCreated = 0
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderName = "folderWithImg"
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + folderName, "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        nestedFolder = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "nestedFolder", "")
        fileFolderOps.createDirectoryIfNotExisting(nestedFolder)        
        #---create dummy files
        generator = ImageGenerator()
        #imageWidth = 200; imageHeight = 100; 
        imageName = "image1.png"
        imageNameWithPath = os.path.join(folderToSearch, imageName)
        #imageNameWithPath = generator.generateRandomRGB_PNG_Image(imageWidth, imageHeight, imageNameWithPath)
        generator.generateRandomBarGraph(imageNameWithPath) #original image
        jpgImageNameWithPath = generator.convertThisImageToJPG(imageNameWithPath) 
        numberOfDuplicatesCreated += 1
        webpImageNameWithPath = generator.convertThisImageToWEBP(imageNameWithPath) 
        numberOfDuplicatesCreated += 1        
        fileFolderOps.copyFile(imageNameWithPath, os.path.join(nestedFolder, "copied_" + imageName))
        numberOfDuplicatesCreated += 1
        #---run a search        
        duplicateFinder = imageDuplicateFinder.ImageDuplicateSearch(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchByMovingFile()
        duplicateFinder.search()
        #Note: The asserts below may fail if the accuracy of the imagehash detection algorithm is low
        assert duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found        
        #---check if files were copied to duplicate folder
        duplicates = fileFolderOps.getListOfFilesInThisFolder(os.path.join(folderToSearch, constants.GlobalConstants.duplicateImagesFolder))
        assert len(duplicates) == numberOfDuplicatesCreated + 1 #files found should be equal to the number of duplicates we know were created plus the undo file generated       
        #---check if undo file is present
        undoPresent = False
        for duplicateFileName in duplicates:
            if duplicateFileName.endswith(constants.GlobalConstants.UNDO_FILE_EXTENSION):
                undoPresent = True
        assert undoPresent
    
