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

class ImageGenerator: #https://stackoverflow.com/questions/15261851/100x100-image-with-random-pixel-colour
    def __init__(self):
        self.IMAGE_Z_AXIS = 3
        self.SHADE_RANGE_START = 0
        self.MAX_SHADE_RANGE = 255
        self.MAX_SHADE_RANGE_SANS_ZERO = 256
        self.PNG_Extension = ".png"
        self.JPG_Extension = ".jpg"
    
    def generateRandomRGB_PNG_Image(self, imageWidth, imageHeight, filenameWithPath):
        imageArray = numpy.random.rand(imageHeight, imageWidth, self.IMAGE_Z_AXIS) * self.MAX_SHADE_RANGE
        generatedImage = Image.fromarray(imageArray.astype('uint8')).convert('RGBA')
        if not filenameWithPath.lower().endswith(self.PNG_Extension):
            filenameWithPath = filenameWithPath + self.PNG_Extension
        generatedImage.save(filenameWithPath)
        return filenameWithPath
        
    def convertThisImageToJPG(self, filenameWithPath):
        img = Image.open(filenameWithPath)
        img = img.convert('RGB')
        imageNameWithoutExtension = os.path.splitext(filenameWithPath)[constants.GlobalConstants.FIRST_POSITION_IN_LIST] #The path will be retained but the ".png" will be removed
        filenameWithPath = imageNameWithoutExtension + self.JPG_Extension
        img.save(filenameWithPath)
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
        
    def test_searchingInFolderWithDuplicateImages(self):
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
        imageWidth = 200; imageHeight = 100; imageName = "image1.png"
        imageNameWithPath = os.path.join(folderToSearch, imageName)
        #imageNameWithPath = generator.generateRandomRGB_PNG_Image(imageWidth, imageHeight, imageNameWithPath)
        generator.generateRandomBarGraph(imageNameWithPath)
        jpgImageNameWithPath = generator.convertThisImageToJPG(imageNameWithPath)
        #fileFolderOps.copyFile(imageNameWithPath, os.path.join(nestedFolder, "copiedImage1"))
        #---run a search        
        duplicateFinder = imageDuplicateFinder.ImageDuplicateSearch(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchByMovingFile()
        duplicateFinder.search()
        assert duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found        
        
        # hardcodedSizes = [1024 * 2000, 1, 0] #1024 * 2000 will generate a 2MB file
        # filenames = []
        # numberOfFilesToGenerate = 5 #Ensure the number of files generated are greater than the number of file sizes being hardcoded below
        # for i in range(0, numberOfFilesToGenerate):            
        #     generatedFilename = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "file" + str(i))
        #     randomFileSize = random.randint(0, 1024) #nothing particular about 1024. It could be any other number too
        #     #hardcode some file sizes
        #     if i < len(hardcodedSizes):
        #         randomFileSize = hardcodedSizes[i]
        #     fileFolderOps.generateFileWithRandomData(generatedFilename, randomFileSize)
        #     filenames.append(generatedFilename)
        # #---create duplicates of the hardcoded size files
        # numberOfDuplicatesCreated = 0
        # for i in range(0, len(hardcodedSizes)):
        #     fileFolderOps.copyFile(filenames[i], nestedFolder)
        #     numberOfDuplicatesCreated += 1
        # #---create a symbolic link that should be ignored
        # symlinkFilenameWithPath = os.path.join(folderToSearch, "linkToNowhere")
        # fileFolderOps.deleteFileIfItExists(symlinkFilenameWithPath)
        # os.symlink(folderToSearch, symlinkFilenameWithPath) #Create the symlink file which should be ignored during traversal
        # #---create a duplicate fo the symlink
        # fileFolderOps.copyFile(symlinkFilenameWithPath, nestedFolder) #this copied file should not end up in the duplicates folder
        # #---run a search        
        # duplicateFinder = imageDuplicateFinder.ImageDuplicateSearch(folderToSearch, fileFolderOps)
        # duplicateFinder.switchOffGUI()
        # duplicateFinder.setToSearchByMovingFile()
        # duplicateFinder.search()
        # assert duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        # #---check if files were copied to duplicate folder
        # duplicates = fileFolderOps.getListOfFilesInThisFolder(os.path.join(folderToSearch, constants.GlobalConstants.duplicateFilesFolder))
        # assert len(duplicates) == numberOfDuplicatesCreated + 1 #files found should be equal to the number of duplicates we know were created plus the undo file generated
        # #---check if undo file is present
        # undoPresent = False
        # for duplicateFileName in duplicates:
        #     if duplicateFileName.endswith(constants.GlobalConstants.UNDO_FILE_EXTENSION):
        #         undoPresent = True
        # assert undoPresent
                