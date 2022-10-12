import os
import random
import string
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import uniqueLineFinder
from programConstants import constants

class TestUniqueLineFinding:
    def _generateGibberishTextLine(self):        
        maxLengthOfSentence = 100 #can be much much much larger https://stackoverflow.com/questions/2257441/random-string-generation-with-upper-case-letters-and-digits       
        #database = string.ascii_uppercase + string.digits + string.ascii_lowercase + string.punctuation + string.whitespace
        database = ["betty ", "bought ", "some ", "butter ", 'but ', 'the ', "was ", "bitter ", "so ", "better ", "to ", "make ", "sea ", "shells ", "on ", "shore "]
        lineLength = random.randint(0, maxLengthOfSentence)
        return ''.join(random.choices(database, k = lineLength))
        
    def test_searchingInEmptyFolder(self):#no folder should get created if there were no files to process
        fileFolderOps = fileOps.FileOperations()
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + "folderEmpty", "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        #---run a search        
        uniqueLinesFinder = uniqueLineFinder.UniqueLineFinder(folderToSearch, fileFolderOps)
        uniqueLinesFinder.switchOffGUI()
        uniqueLinesFinder.search()
        assert not uniqueLinesFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        assert not fileFolderOps.isThisValidDirectory(uniqueLinesFinder.getFolderToStoreConsolidatedFile()) #this folder should not have got created if no files were found to process
       
    def test_generateDuplicateLineFilesAndSearch(self):#positive test
        CHANCES_OF_MATCHING = 10 #how less often a duplicate sentence should be created
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderName = "folderWithDuplicateLines"
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + folderName, "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        nestedFolder = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "nestedFolder", "")
        fileFolderOps.createDirectoryIfNotExisting(nestedFolder)  
        fileSuffix = ".txt"      
        #---create files containing duplicate sentences
        uniqueLines = set()
        numberOfFilesToGenerate = 5
        maxLinesToGenerate = 500
        averageNumberOfLines = maxLinesToGenerate / numberOfFilesToGenerate
        numberOfLinesWritten = 0
        numberOfDuplicateSentencesInserted = 0
        for i in range(numberOfFilesToGenerate): #each file to generate 
            generatedFilename = constants.GlobalConstants.dummyPrefix + "file" + str(i) + fileSuffix
            print("--- processing file: ", generatedFilename)
            if random.randint(0, 1) == 0:#to sometimes place the file in a subfolder
                generatedFilename = os.path.join(folderToSearch, generatedFilename)
            else:
                generatedFilename = os.path.join(nestedFolder, generatedFilename)            
            numberOfLinesForThisFile = random.randint(0, averageNumberOfLines)
            linesToWriteInThisFile = []
            for _ in range(numberOfLinesForThisFile):
                line = self._generateGibberishTextLine() 
                linesToWriteInThisFile.append(line) 
                
                if line in uniqueLines:#line already exists. Duplicate sentences can exist in the same file too
                    numberOfDuplicateSentencesInserted += 1
                    print("Incremented duplicate after creating an exact match")
                else:
                    uniqueLines.add(line)
                    print("Added", line, "to uniqueLines")
                    if random.randint(0, CHANCES_OF_MATCHING) == 0 and uniqueLines:#random condition and uniqueLines is not empty
                        toadd = random.choice(tuple(uniqueLines))
                        linesToWriteInThisFile.append(toadd) #choose an existing line and insert it as a duplicate
                        numberOfDuplicateSentencesInserted += 1
                        print("Incremented duplicate. Added: ", toadd)                
                                  
            print("writing", str(linesToWriteInThisFile))
            numberOfLinesWritten += len(linesToWriteInThisFile)
            fileFolderOps.writeLinesToFile(generatedFilename, linesToWriteInThisFile) #if list is empty, an empty file will get created on disk
        
        #---perform the line duplicate check and verify 
        lineFinder = uniqueLineFinder.UniqueLineFinder(folderToSearch, fileFolderOps)
        lineFinder.switchOffGUI()
        lineFinder.search()
        assert numberOfDuplicateSentencesInserted == lineFinder.howManyDuplicatesFound()
        
        
