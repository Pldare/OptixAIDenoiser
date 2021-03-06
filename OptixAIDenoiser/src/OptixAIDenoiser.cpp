// Chris Chen (DEM) crscrs@live.com

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>

#include "optix_world.h"

#include "IL/il.h"
#include "IL/ilu.h"


#include "OptixAIDenoiser.h"

using namespace std;

BYTE* _denoiseImplement(char input_path[], char output_path[], float blend, bool is_batch)
{

	int width, height;
	ILboolean b_loaded;
	BYTE* data = ilGetData();
	b_loaded = false;

	// Load image file
	ilBindImage(inputImage);
	
	 
	b_loaded = ilLoadImage(input_path);
	iluFlipImage();

	//progress_callback(0.05f);

	// Prepare image data
	ilBindImage(inputImage);
	width = ilGetInteger(IL_IMAGE_WIDTH);
	height = ilGetInteger(IL_IMAGE_HEIGHT);
	
	int type = ilGetInteger(IL_IMAGE_TYPE);
	int format = ilGetInteger(IL_IMAGE_FORMAT);
	int bpp = ilGetInteger(IL_IMAGE_BITS_PER_PIXEL);
	int chl = ilGetInteger(IL_IMAGE_CHANNELS);
	
	cout << "Width : " << width << ", ";
	cout << "Height : " << height << ", ";
	cout << "Color Depth : " << bpp / chl << endl;

	ilConvertImage(IL_RGBA, IL_FLOAT);
	int Bpp = ilGetInteger((IL_IMAGE_BPP));	

	// Copy all image data to the gpu buffers
	ilBindImage(inputImage);
	data = ilGetData();
	memcpy(input_buffer->map(), data, sizeof(float) * 4 * width * height);
	input_buffer->unmap();

	// Execute denoise
	std::cout << "Denoising..." << std::endl;
	commandList->execute();
	std::cout << "Denoising complete" << std::endl;

	// Create ouput image
	ilBindImage(outputImage);
	ilTexImage(width, height, 0, 4, IL_RGBA, IL_FLOAT, NULL);

	// Copy denoised image back to the cpu
	ilBindImage(outputImage);
	data = ilGetData();
	memcpy(data, output_buffer->map(), sizeof(float) * 4 * width * height);
	output_buffer->unmap();
	ilConvertImage(format, type);	
	//ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
	data = ilGetData();
	// If the image already exists delete it
	remove(output_path);

	// Save the output image

	//if (string(output_path).find("exr") != -1)
	//{
		/*
		ilBindImage(outputImage);
		ilConvertImage(IL_BGRA, IL_FLOAT);
		data = ilGetData();
		float *tmp = (float *)data;

		for (int i = 0; i < width * height; i++)
		{
			exrimages[0][i] = tmp[4 * i + 0];
			exrimages[1][i] = tmp[4 * i + 1];
			exrimages[2][i] = tmp[4 * i + 2];
			exrimages[3][i] = tmp[4 * i + 3];
		}

		exrimage.images = (BYTE**)exrimages;
		exrimage.width = width;
		exrimage.height = height;

		exrheader.num_channels = 4;
		exrheader.channels = new EXRChannelInfo[exrheader.num_channels];
		// Must be BGR(A) order, since most of EXR viewers expect this channel order.
		strncpy_s(exrheader.channels[0].name, "B", 255); exrheader.channels[0].name[strlen("B")] = '\0';
		strncpy_s(exrheader.channels[1].name, "G", 255); exrheader.channels[1].name[strlen("G")] = '\0';
		strncpy_s(exrheader.channels[2].name, "R", 255); exrheader.channels[2].name[strlen("R")] = '\0';
		strncpy_s(exrheader.channels[3].name, "A", 255); exrheader.channels[3].name[strlen("A")] = '\0';

		exrheader.pixel_types = new int[exrheader.num_channels];
		exrheader.requested_pixel_types = new int[exrheader.num_channels];
		for (int i = 0; i < exrheader.num_channels; i++) {
			exrheader.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of input image
			exrheader.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of output image to be stored in .EXR
		}

		const char* err;
		int ret = SaveEXRImageToFile(&exrimage, &exrheader, output_path, &err);*/
	//}
	//else
	//{
		if (!ilSaveImage(output_path))
			return NULL;
	//}
	
	if (!is_batch)
	{
		ilBindImage(outputImage);
		ilConvertImage(IL_BGRA, IL_UNSIGNED_BYTE);
		Bpp = ilGetInteger(IL_IMAGE_BPP);

		data = ilGetData();
		int size = height * width * Bpp;
		memcpy(sdata, data, size);
		return sdata;
	}	

#ifdef _DEBUG
	//cout << "Color depth :" << bpp * 8 << endl << "Byte data size :" << size << endl;
#endif // _DEBUG
	
	return NULL;
}

void _setUpContext()
{
	ilInit();
	iluInit();
	ilGenImages(1, &inputImage);
	ilGenImages(1, &outputImage);


	cout << "Denoiser Initialized" << endl;

}

void _jobStart(int width, int height, float blend)
{
	// Create optix image buffers

	optix_context = optix::Context::create();
	input_buffer = optix_context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, width, height);
	output_buffer = optix_context->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT4, width, height);

	// Setup the optix denoiser post processing stage
	denoiserStage = optix_context->createBuiltinPostProcessingStage("DLDenoiser");
	denoiserStage->declareVariable("input_buffer")->set(input_buffer);
	denoiserStage->declareVariable("output_buffer")->set(output_buffer);
	denoiserStage->declareVariable("blend")->setFloat(blend);

	// Add the denoiser to the new optix command list
	commandList = optix_context->createCommandList();
	commandList->appendPostprocessingStage(denoiserStage, width, height);
	commandList->finalize();
	// Compile context. I'm not sure if this is needed given there is no megakernal?
	optix_context->validate();
	optix_context->compile();
}

void _jobComplete()
{
	denoiserStage->destroy();
	commandList->destroy();

	input_buffer->destroy();
	output_buffer->destroy();
	optix_context->destroy();
}

void _cleantUpContext()
{
	ilDeleteImages(1, &inputImage);
	ilDeleteImages(1, &outputImage);
	ilShutDown();
	iluImageParameter(ILU_FILTER, ILU_SCALE_LANCZOS3);
	

	cout << "Denoiser Destroyed" << endl;

}

int _getWidth(char file_path[])
{

	// Load image file
	ilBindImage(inputImage);
	bool b_loaded = ilLoadImage(file_path);
	if (b_loaded)
	{
		return ilGetInteger(IL_IMAGE_WIDTH);
	}
	else
		return -1;

}

int _getHeight(char file_path[])
{

	// Load image file
	ilBindImage(inputImage);
	bool b_loaded = ilLoadImage(file_path);
	if (b_loaded)
	{
		return ilGetInteger(IL_IMAGE_HEIGHT);
	}
	else
		return -1;
}