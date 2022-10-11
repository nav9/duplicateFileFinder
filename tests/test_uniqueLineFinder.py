import os
import random
import string
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import uniqueLineFinder
from programConstants import constants

class TestUniqueLineFinding:
    def _generateGibberishTextLine(self):        
        maxLengthOfSentence = 50 #can be much much much larger https://stackoverflow.com/questions/2257441/random-string-generation-with-upper-case-letters-and-digits       
        database = string.ascii_uppercase + string.digits + string.ascii_lowercase + string.punctuation + string.whitespace
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
        lines = set()
        filenames = []
        numberOfFilesToGenerate = 20
        maxLinesToGenerate = 100
        averageNumberOfLines = maxLinesToGenerate / numberOfFilesToGenerate
        numberOfLinesWritten = 0
        numberOfDuplicateSentencesInserted = 0        
        for i in range(numberOfFilesToGenerate): #each file to generate   
            if random.randint(0, 1) == 0:#to sometimes place the file in a subfolder
                generatedFilename = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "file" + str(i) + fileSuffix)
            else:
                generatedFilename = os.path.join(nestedFolder, constants.GlobalConstants.dummyPrefix + "file" + str(i) + fileSuffix)            
            filenames.append(generatedFilename)
            numberOfLinesForThisFile = random.randint(0, averageNumberOfLines)
            maxLinesToGenerate = maxLinesToGenerate - numberOfLinesForThisFile
            linesToWrite = []
            for _ in range(numberOfLinesForThisFile):
                line = self._generateGibberishTextLine() 
                if line in lines:#line already exists. Duplicate sentences can exist in the same file too
                    numberOfDuplicateSentencesInserted += 1
                else:
                    if random.randint(0, CHANCES_OF_MATCHING) == 0:
                        linesToWrite.append(random.choice(tuple(lines))) #choose an existing line and insert it as a duplicate
                        numberOfDuplicateSentencesInserted += 1
                lines.add(line)
                linesToWrite.append(line)                                 
            fileFolderOps.writeLinesToFile(generatedFilename, linesToWrite)
            numberOfLinesWritten += len(linesToWrite)
        
        #---perform the line duplicate check and verify 
        lineFinder = uniqueLineFinder.UniqueLineFinder(folderToSearch, fileFolderOps)
        lineFinder.switchOffGUI()
        lineFinder.search()
        assert numberOfDuplicateSentencesInserted == lineFinder.howManyDuplicatesFound()
        
        
